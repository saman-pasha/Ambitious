;;; A local stand-in for the `str' system (cl-str), holding only the ten
;;; functions Cicili's transpiler calls. See str.lisp and toolchain/README.md:
;;; it exists so the toolchain can be built from distribution packages alone,
;;; without fetching Quicklisp. Where Quicklisp's `str' is installed, ASDF may
;;; find either; both answer the same for every call Cicili makes.
(defsystem "str"
  :description "Ambitious toolchain shim: the subset of cl-str that Cicili uses."
  :version "0.0.1"
  :depends-on ("cl-ppcre")
  :components ((:file "str")))
