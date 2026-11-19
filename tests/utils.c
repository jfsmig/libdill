/*

  Copyright (c) 2026 Jean-Francois Smigielski

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom
  the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.

*/

#include "assert.h"
#include "../utils.c"

int main(void) {
    /* dill_rstrip returns the position after the last character that is not
       the delimiter, which is the start of a string that has no such
       character. */
    const char *padded = "  ab c  ";
    assert(dill_rstrip(padded, ' ') == padded + 6);
    const char *unpadded = "ab c";
    assert(dill_rstrip(unpadded, ' ') == unpadded + 4);
    const char *empty = "";
    assert(dill_rstrip(empty, ' ') == empty);
    const char *single = " ";
    assert(dill_rstrip(single, ' ') == single);
    const char *blank = "   ";
    assert(dill_rstrip(blank, ' ') == blank);

    return 0;
}
