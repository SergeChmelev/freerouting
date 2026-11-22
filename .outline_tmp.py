import sys, re
# Print a Java file outline: class-level declarations only (method bodies elided).
# Javadoc collapsed to its first text line.
def strip_strings(l):
    l = re.sub(r'"(\\.|[^"\\])*"', '""', l)
    l = re.sub(r"'(\\.|[^'\\])*'", "''", l)
    return l
for path in sys.argv[1:]:
    print("##### " + path)
    src = open(path, encoding='utf-8').read().split('\n')
    depth = 0
    type_depths = [0]  # depths at which class bodies open
    in_block_comment = False
    jd_first = None
    pending_type = False
    for raw in src:
        line = raw
        s = line.strip()
        if in_block_comment:
            if jd_first is None and s.strip('* /') :
                jd_first = s.strip('* /')
            if '*/' in s:
                in_block_comment = False
            continue
        if s.startswith('/*'):
            jd_first = s.strip('/* ') or None
            if '*/' not in s:
                in_block_comment = True
            continue
        if s.startswith('import ') or s.startswith('package '):
            continue
        code = strip_strings(re.sub(r'//.*', '', line))
        cur_is_typelevel = depth in type_depths
        if cur_is_typelevel and s and not s.startswith('//'):
            if jd_first:
                print('  ' * 0 + ' ' * (depth*2) + '// ' + jd_first[:110])
                jd_first = None
            print(line.rstrip())
        elif cur_is_typelevel and s.startswith('//') :
            pass
        # type detection
        if cur_is_typelevel and re.search(r'\b(class|interface|enum|record)\s+\w+', code):
            pending_type = True
        for ch in code:
            if ch == '{':
                depth += 1
                if pending_type:
                    type_depths.append(depth)
                    pending_type = False
            elif ch == '}':
                if depth in type_depths and depth != 0:
                    type_depths.remove(depth)
                depth -= 1
        if not cur_is_typelevel and depth in type_depths and s.startswith('}'):
            pass
