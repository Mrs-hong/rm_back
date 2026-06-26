#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
会议文档生成器 - CLI 入口

用法:
  python cli.py note -c content.html -t "标题" -o output.docx
  python cli.py summary -c content.html -d '{"theme":"会议"}' -m 2 -o output.docx
  python cli.py package -f file1.docx file2.docx -n "文档包" -o ./output
"""

import argparse
import json
import os
import sys

# 确保能导入同目录下的 meeting_doc_gen 包
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from meeting_doc_gen import MeetingDocGenerator


def cmd_note(args):
    """生成笔记"""
    gen = MeetingDocGenerator()
    with open(args.content, 'r', encoding='utf-8') as f:
        content = f.read()
    success, result, err = gen.build_note(
        content, title=args.title, content_type=args.type, output_path=args.output
    )
    if success:
        print(f"笔记生成成功: {result}")
    else:
        print(f"笔记生成失败: {err}")
        sys.exit(1)


def cmd_summary(args):
    """生成纪要"""
    gen = MeetingDocGenerator()
    with open(args.content, 'r', encoding='utf-8') as f:
        content = f.read()
    meeting_data = json.loads(args.data)
    success, result, err = gen.build_summary(
        content, meeting_data=meeting_data,
        template_type=args.template, content_type=args.type, output_path=args.output
    )
    if success:
        print(f"纪要生成成功: {result}")
    else:
        print(f"纪要生成失败: {err}")
        sys.exit(1)


def cmd_package(args):
    """打包 ZIP"""
    gen = MeetingDocGenerator()
    files = []
    for file_path in args.files:
        with open(file_path, 'rb') as f:
            files.append((os.path.basename(file_path), f.read()))
    success, result, err = gen.package_to_zip(files, args.name, args.output)
    if success:
        print(f"打包成功: {result}")
    else:
        print(f"打包失败: {err}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description='会议文档生成器')
    subparsers = parser.add_subparsers(dest='command', help='子命令')

    # note 子命令
    note_parser = subparsers.add_parser('note', help='生成笔记')
    note_parser.add_argument('--content', '-c', required=True, help='笔记内容文件路径')
    note_parser.add_argument('--title', '-t', default='笔记', help='文档标题')
    note_parser.add_argument('--type', default='auto',
                             choices=['auto', 'html', 'markdown', 'text', 'editorjs'],
                             help='内容类型')
    note_parser.add_argument('--output', '-o', required=True, help='输出文件路径')

    # summary 子命令
    summary_parser = subparsers.add_parser('summary', help='生成纪要')
    summary_parser.add_argument('--content', '-c', required=True, help='纪要内容文件路径')
    summary_parser.add_argument('--data', '-d', default='{}', help='会议元数据 JSON')
    summary_parser.add_argument('--template', '-m', type=int, default=1,
                                choices=[1, 2, 3], help='模板类型: 1=白板 2=会议纪要 3=联合行文')
    summary_parser.add_argument('--type', default='auto',
                                choices=['auto', 'html', 'markdown', 'editorjs'],
                                help='内容类型')
    summary_parser.add_argument('--output', '-o', required=True, help='输出文件路径')

    # package 子命令
    package_parser = subparsers.add_parser('package', help='打包为 ZIP')
    package_parser.add_argument('--files', '-f', nargs='+', required=True, help='要打包的文件路径列表')
    package_parser.add_argument('--name', '-n', default='文档包', help='ZIP 文件名')
    package_parser.add_argument('--output', '-o', default='./output', help='输出目录')

    args = parser.parse_args()

    commands = {
        'note': cmd_note,
        'summary': cmd_summary,
        'package': cmd_package,
    }

    if args.command in commands:
        commands[args.command](args)
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
