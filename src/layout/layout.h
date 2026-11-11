/* src/layout/layout.h -- the codes the layout module's records keep in their plain
   fields (contracts/layout.pl), for the units that read the box tree: paint. */
#ifndef AMB_LAYOUT_H
#define AMB_LAYOUT_H

/* a border's style, a side each (box fields bst, bsr, bsb, bsl), as CSS names them */
enum { LAY_BS_NONE, LAY_BS_HIDDEN, LAY_BS_DOTTED, LAY_BS_DASHED, LAY_BS_SOLID, LAY_BS_DOUBLE, LAY_BS_GROOVE, LAY_BS_RIDGE,
       LAY_BS_INSET, LAY_BS_OUTSET };

#endif
