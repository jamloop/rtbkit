import argparse
import sys
import datetime

DEFAULT_OUT = 'content_category.itm'

def extract_from_parts(parts):
    cat_parts = parts[0].split('=')
    cat_id = cat_parts[0]

    cat_desc = parts[1].replace('//', '').strip()
    return (cat_id, cat_desc)

def main():
    parser = argparse.ArgumentParser(description='Generate the ContentCategory Item List')
    parser.add_argument('--proto', dest='proto', required=True, help="The protobuf file")
    parser.add_argument('--out', dest='out', default=DEFAULT_OUT, help="The output file name")

    args = parser.parse_args()

    categories = []
    in_enum = False
    with open(args.proto) as proto_file:
        for line in proto_file:
            stripped_line = line.strip()
            if in_enum:
                if stripped_line == '}': break
                if stripped_line.startswith('//'): continue

                parts = stripped_line.split(';')
                categories.append(extract_from_parts(parts))

            if stripped_line == 'enum ContentCategory {':
                 in_enum = True

    if len(categories) == 0:
        print "Failed to find the ContentCategory enum in the proto file"
    else:
        with open(args.out, 'w') as out_file:
            out_file.write('/* This file was automatically generated. Do not attempt to modify it */\n')
            for cat in categories:
                out_file.write('ITEM({id}, "{desc}")\n'.format(id=cat[0], desc=cat[1]))

            now = datetime.datetime.now()
            out_file.write('/* Generated from {proto_file} by gen-contentcategory.py on {date} */'.format(
                proto_file=args.proto, date=now.strftime('%Y/%m/%d %H:%M:%S')))

    return 0

if __name__ == '__main__':
    sys.exit(main())
