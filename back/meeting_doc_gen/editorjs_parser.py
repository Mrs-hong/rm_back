#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""会议文档生成器 - Editor.js JSON 解析器"""

import json
from typing import Any, Dict, List, Optional, Union

from bs4 import BeautifulSoup

from .models import (
    Alignment, DocumentNode, ParagraphNode, TableCell, TableNode,
)
from .html_parser import _parse_inline_elements


class EditorJsParser:
    """Editor.js JSON 解析器"""

    def parse(self, editorjs_data: Union[str, Dict]) -> DocumentNode:
        """解析 Editor.js JSON 为文档节点"""
        if isinstance(editorjs_data, str):
            editorjs_data = json.loads(editorjs_data)

        doc_node = DocumentNode()
        section = doc_node.add_section()
        blocks = editorjs_data.get('blocks', [])

        for block in blocks:
            node = self._parse_block(block)
            if node:
                if isinstance(node, list):
                    section.body.extend(node)
                else:
                    section.body.append(node)

        return doc_node

    def _parse_block(self, block: Dict) -> Optional[Union[ParagraphNode, TableNode, List[ParagraphNode]]]:
        """解析单个 block"""
        block_type = block.get('type', '')
        data = block.get('data', {})

        handlers = {
            'header': self._parse_header,
            'paragraph': self._parse_paragraph,
            'list': self._parse_list,
            'delimiter': self._parse_delimiter,
            'table': self._parse_table,
            'quote': self._parse_quote,
            'code': self._parse_code,
        }
        handler = handlers.get(block_type)
        return handler(data) if handler else None

    def _parse_header(self, data: Dict) -> ParagraphNode:
        level = data.get('level', 2)
        text = data.get('text', '')
        font_sizes = {1: 22, 2: 16, 3: 16, 4: 16, 5: 14, 6: 14}
        font_names = {1: '方正小标宋简体', 2: '黑体', 3: '楷体', 4: '仿宋', 5: '仿宋', 6: '仿宋'}
        para = ParagraphNode(
            alignment=Alignment.CENTER if level <= 2 else Alignment.LEFT,
            line_spacing=1.0, space_after=8
        )
        para.add_run(text, font_name=font_names.get(level, '黑体'),
                     font_size=font_sizes.get(level, 16), bold=(level <= 2))
        return para

    def _parse_paragraph(self, data: Dict) -> ParagraphNode:
        text = data.get('text', '')
        para = ParagraphNode(first_line_indent=32, line_spacing=1.0)
        soup = BeautifulSoup(text, 'html.parser')
        runs = _parse_inline_elements(soup)
        if runs:
            for run_data in runs:
                t = run_data.get('text', '')
                if t:
                    s = run_data.get('style', {})
                    para.add_run(t, font_name=s.get('font_name', '仿宋'),
                                 font_size=s.get('font_size', 16),
                                 bold=s.get('bold', False),
                                 italic=s.get('italic', False),
                                 underline=s.get('underline', False))
        elif text:
            para.add_run(text, font_name='仿宋', font_size=16)
        return para

    def _parse_list(self, data: Dict) -> List[ParagraphNode]:
        style = data.get('style', 'unordered')
        items = data.get('items', [])
        result = []
        for i, item in enumerate(items):
            prefix = f"{i + 1}. " if style == 'ordered' else "• "
            para = ParagraphNode(left_indent=32, first_line_indent=-16, line_spacing=1.0)
            para.add_run(prefix, font_name='仿宋', font_size=16)
            soup = BeautifulSoup(item, 'html.parser')
            runs = _parse_inline_elements(soup)
            if runs:
                for run_data in runs:
                    t = run_data.get('text', '')
                    if t:
                        s = run_data.get('style', {})
                        para.add_run(t, font_name=s.get('font_name', '仿宋'),
                                     font_size=s.get('font_size', 16),
                                     bold=s.get('bold', False))
            else:
                para.add_run(item, font_name='仿宋', font_size=16)
            result.append(para)
        return result

    def _parse_delimiter(self) -> ParagraphNode:
        para = ParagraphNode(alignment=Alignment.CENTER, line_spacing=1.0)
        para.add_run('—' * 20, font_name='仿宋', font_size=16)
        return para

    def _parse_table(self, data: Dict) -> TableNode:
        content = data.get('content', data.get('withHeadings', data))
        rows_data = content if isinstance(content, list) else []
        table_node = TableNode()
        for row_data in rows_data:
            row = []
            cells = row_data if isinstance(row_data, list) else []
            for cell_text in cells:
                cell = TableCell()
                para = ParagraphNode(alignment=Alignment.LEFT, line_spacing=1.0)
                para.add_run(str(cell_text), font_name='仿宋', font_size=16)
                cell.paragraphs.append(para)
                row.append(cell)
            table_node.rows.append(row)
        return table_node

    def _parse_quote(self, data: Dict) -> ParagraphNode:
        text = data.get('text', '')
        caption = data.get('caption', '')
        alignment = data.get('alignment', 'left')
        align_map = {'left': Alignment.LEFT, 'center': Alignment.CENTER, 'right': Alignment.RIGHT}
        para = ParagraphNode(alignment=align_map.get(alignment, Alignment.LEFT),
                             left_indent=20, line_spacing=1.0)
        para.add_run(text, font_name='楷体', font_size=16, italic=True)
        if caption:
            para.add_run(f"\n— {caption}", font_name='仿宋', font_size=14)
        return para

    def _parse_code(self, data: Dict) -> ParagraphNode:
        code = data.get('code', '')
        para = ParagraphNode(left_indent=20, line_spacing=1.0)
        para.add_run(code, font_name='Courier New', font_size=12)
        return para
