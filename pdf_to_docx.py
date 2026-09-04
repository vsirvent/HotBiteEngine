"""Convert a PDF file to DOCX.

Usage:
    python pdf_to_docx.py <input.pdf> [output.docx]

If output.docx is omitted, it is written next to the input file with the
same base name.
"""
import sys
from pathlib import Path
from typing import Optional

from pdf2docx import Converter


def convert(pdf_path: str, docx_path: Optional[str] = None) -> str:
    src = Path(pdf_path)
    if not src.is_file():
        raise FileNotFoundError(f"Input PDF not found: {src}")

    dst = Path(docx_path) if docx_path else src.with_suffix(".docx")

    cv = Converter(str(src))
    try:
        cv.convert(str(dst))
    finally:
        cv.close()

    return str(dst)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    pdf_arg = sys.argv[1]
    docx_arg = sys.argv[2] if len(sys.argv) > 2 else None

    out = convert(pdf_arg, docx_arg)
    print(f"Converted: {out}")
