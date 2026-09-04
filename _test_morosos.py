import re
from collections import Counter

RECORD_START_RE = re.compile(r'^(\d{4,7})\s+(\S+)\s+(\d{2}/\d{2}/\d{4})\s*(.*)$')
AMOUNT_LINE_RE = re.compile(r'^([\d.]+,\d{2})\s+([\d.]+,\d{2})\s+([\d.]+,\d{2})\s+([\d.]+,\d{2})$')
PAGE_HEADER_RE = re.compile(r'^Recibos de comunidades')
COL_HEADER_RE = re.compile(r'^Depart\.\s+Recibo\s+Est\.')
COMUNIDAD_RE = re.compile(r'^COMUNIDAD:')
SUBTOTAL_TRIGGER_RE = re.compile(r'TOTAL:?\s*$|TOTAL:|^C\.P[\s.]|^TOTAL\s')

def parse_morosos(path, encoding='cp1252'):
    with open(path, encoding=encoding, errors='replace') as fh:
        lines = [l.rstrip('\r\n') for l in fh]

    records = []
    warnings = []
    location_buf = []
    current_location = ''
    in_record = False
    concept_buf = []
    rec_meta = None
    skip_mode = False

    def flush_location():
        nonlocal current_location, location_buf
        text = ' '.join(x.strip() for x in location_buf if x.strip())
        if text:
            current_location = text
        location_buf = []

    for i, raw in enumerate(lines):
        line = raw.strip()
        if not line:
            continue
        if PAGE_HEADER_RE.match(line) or COL_HEADER_RE.match(line) or COMUNIDAD_RE.match(line):
            skip_mode = False
            continue
        if SUBTOTAL_TRIGGER_RE.search(line):
            skip_mode = True
            if in_record:
                warnings.append(('record abierto interrumpido por subtotal', i+1, line))
                in_record = False
                concept_buf = []
                rec_meta = None
            continue
        if skip_mode:
            if RECORD_START_RE.match(line):
                skip_mode = False
            else:
                continue

        m = RECORD_START_RE.match(line)
        if m and not in_record:
            flush_location()
            rec_meta = {
                'recibo': m.group(1),
                'estado': m.group(2),
                'fecha': m.group(3),
                'location': current_location,
            }
            concept_buf = [m.group(4)] if m.group(4) else []
            in_record = True
            continue

        if in_record:
            am = AMOUNT_LINE_RE.match(line)
            if am:
                concept = ' '.join(x.strip() for x in concept_buf if x.strip())
                bracket_m = re.search(r'\[([^\]]+)\]', concept)
                rec = dict(rec_meta)
                rec['concepto'] = concept
                rec['bracket'] = bracket_m.group(1) if bracket_m else None
                rec['total'] = am.group(1)
                rec['cobrado'] = am.group(2)
                rec['compens'] = am.group(3)
                rec['pendiente'] = am.group(4)
                records.append(rec)
                in_record = False
                concept_buf = []
                rec_meta = None
                continue
            else:
                concept_buf.append(line)
                continue

        # not in record, not a trigger -> location text
        location_buf.append(line)

    return records, warnings

records, warnings = parse_morosos('E:/morosos.csv')
print('total records:', len(records))
print('warnings:', len(warnings))
for w in warnings[:10]:
    print(' warn:', w)

no_bracket = [r for r in records if not r['bracket']]
print('records without bracket tag:', len(no_bracket))
for r in no_bracket[:5]:
    print(' nobracket sample:', r)

# unique locations / bracket groups
locs = Counter(r['location'] for r in records)
print('distinct raw locations:', len(locs))
brackets = Counter(r['bracket'] for r in records if r['bracket'])
print('distinct bracket tags:', len(brackets))
print(list(brackets.items())[:15])

estados = Counter(r['estado'] for r in records)
print('estados:', estados)
