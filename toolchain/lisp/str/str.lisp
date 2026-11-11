;;; The subset of cl-str (https://github.com/vindarel/cl-str) that Cicili's
;;; transpiler calls, written from that library's documented behaviour:
;;;
;;;   containsp count-substring ends-with-p join replace-all replace-first
;;;   s-first split starts-with-p substring
;;;
;;; The details Cicili's output depends on are kept:
;;;   - REPLACE-ALL and REPLACE-FIRST take their arguments literally (the
;;;     replacement too: no \1 back-references) unless :REGEX is true, where the
;;;     pattern is a regex and the replacement goes to ppcre as it is;
;;;   - SPLIT splits on a literal separator unless :REGEX is true, and keeps
;;;     trailing empty strings (cl-str passes ppcre:split a :limit of one more
;;;     than the string's length, which turns Perl's trailing-empty removal off);
;;;   - SUBSTRING takes (start end s), clamps both ends, accepts T or NIL as
;;;     the end and a negative end counted from the back;
;;;   - JOIN prints each element with ~A, as cl-str's FORMAT directive does.
(defpackage :str
  (:use :cl)
  (:export #:containsp #:count-substring #:ends-with-p #:join #:replace-all
           #:replace-first #:s-first #:split #:starts-with-p #:substring))

(in-package :str)

(defun empty-string-p (s)
  (or (null s) (zerop (length s))))

(defun containsp (substring s &key ignore-case)
  "T if S contains SUBSTRING."
  (let ((a (string substring))
        (b (string s)))
    (and (if ignore-case
             (search a b :test #'char-equal)
             (search a b))
         t)))

(defun count-substring (substring s &key (start 0) (end nil))
  "The number of non-overlapping occurrences of SUBSTRING in S."
  (unless (or (null s) (null substring) (empty-string-p substring))
    (loop :with n := (length substring)
          :for position := (search substring s :start2 start :end2 end)
            :then (search substring s :start2 (+ position n) :end2 end)
          :while position
          :sum 1)))

(defun starts-with-p (start s &key ignore-case)
  "T if S begins with START."
  (when (>= (length s) (length start))
    (funcall (if ignore-case #'string-equal #'string=)
             s start :start1 0 :end1 (length start))))

(defun ends-with-p (end s &key ignore-case)
  "T if S ends with END."
  (let ((sl (length s))
        (el (length end)))
    (when (>= sl el)
      (funcall (if ignore-case #'string-equal #'string=)
               s end :start1 (- sl el)))))

(defun join (separator strings)
  "The elements of STRINGS printed with ~A, SEPARATOR between each two."
  (with-output-to-string (out)
    (loop :for (x . more) :on strings
          :do (format out "~A" x)
              (when more (write-string (string separator) out)))))

(defun replace-all (old new s &key regex)
  "Every occurrence of OLD in S replaced by NEW. Both are literal unless REGEX,
where OLD is a regex and NEW may hold back-references, as in cl-str."
  (if regex
      (ppcre:regex-replace-all old s new)
      (ppcre:regex-replace-all (ppcre:quote-meta-chars old) s (list new))))

(defun replace-first (old new s &key regex)
  "The first occurrence of OLD in S replaced by NEW; literal unless REGEX."
  (if regex
      (ppcre:regex-replace old s new)
      (ppcre:regex-replace (ppcre:quote-meta-chars old) s (list new))))

(defun s-first (s)
  "The first character of S as a string; \"\" for an empty one, NIL for NIL."
  (cond ((null s) nil)
        ((empty-string-p s) "")
        (t (subseq s 0 1))))

(defun split (separator s &key omit-nulls limit (start 0) end regex)
  "S split at each SEPARATOR, a literal string or character unless REGEX."
  (let* ((limit (or limit (1+ (length s))))
         (parts (if regex
                    (ppcre:split separator s :limit limit :start start :end end)
                    (ppcre:split `(:sequence ,(string separator)) s
                                 :limit limit :start start :end end))))
    (if omit-nulls
        (remove-if #'empty-string-p parts)
        parts)))

(defun substring (start end s)
  "S from START to END, clamped; END may be NIL or T for the end of S, and a
negative END counts from the back."
  (let* ((n (length s))
         (end (if (or (null end) (eq end t)) n end)))
    (setf start (max 0 start))
    (if (> start n)
        ""
        (progn
          (setf end (min end n))
          (when (< end (- n)) (setf end 0))
          (when (< end 0) (setf end (+ n end)))
          (if (< end start) "" (subseq s start end))))))
