#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""会议文档生成器 - 文档构建器（HTML / Markdown → DocumentNode）"""

from typing import Optional

from .models import (
    Alignment, DocumentNode, ParagraphNode, TableCell, TableNode,
)
from .html_parser import parse_html, parse_markdown


# 国标字体常量
TITLE_FONT = '方正小标宋简体'
HEADING_FONT = '黑体'
BODY_FONT = '仿宋'
TITLE_SIZE = 22
BODY_SIZE = 16
LINE_SPACING = 1.0


def build_from_html(html_content: str, title: Optional[str] = None) -> DocumentNode:
    """从 HTML 构建文档结构（符合 GB/T 9704-2012）"""
    doc_node = DocumentNode()
    section = doc_node.add_section()

    if title:
        para = ParagraphNode(alignment=Alignment.CENTER, line_spacing=LINE_SPACING, space_after=16)
        para.add_run(title, font_size=TITLE_SIZE, bold=False, font_name=TITLE_FONT)
        section.body.append(para)

    nodes = parse_html(html_content)
    for node in nodes:
        node_type = node.get('type', 'p')

        if node_type == 'table':
            _build_table(section, node)
        elif node_type in ('ul', 'ol'):
            _build_list(section, node)
        else:
            _build_paragraph(section, node)

    return doc_node


def build_from_markdown(md_content: str, title: Optional[str] = None) -> DocumentNode:
    """从 Markdown 构建文档结构（符合 GB/T 9704-2012）"""
    doc_node = DocumentNode()
    section = doc_node.add_section()

    if title:
        para = ParagraphNode(alignment=Alignment.CENTER, line_spacing=LINE_SPACING, space_after=16)
        para.add_run(title, font_size=TITLE_SIZE, bold=False, font_name=TITLE_FONT)
        section.body.append(para)

    nodes = parse_markdown(md_content)
    for node in nodes:
        node_type = node.get('type', 'p')

        if node_type == 'table':
            _build_table(section, node)
        elif node_type in ('ul', 'ol'):
            _build_md_list(section, node)
        else:
            _build_md_paragraph(section, node)

    return doc_node


# ---------------------------------------------------------------------------
# 内部构建辅助
# ---------------------------------------------------------------------------

def _build_table(section, node):
    """构建表格"""
    rows = node.get('rows', [])
    if not rows:
        return
    table_node = TableNode()
    for row_idx, row in enumerate(rows):
        table_row = []
        for cell_data in row:
            cell_text = cell_data.get('text', '')
            is_header = cell_data.get('is_header', False)
            cell = TableCell()
            para = ParagraphNode(
                alignment=Alignment.CENTER if is_header or row_idx == 0 else Alignment.LEFT,
                line_spacing=LINE_SPACING
            )
            if cell_text:
                para.add_run(cell_text, font_name=BODY_FONT, font_size=BODY_SIZE,
                             bold=(is_header or row_idx == 0))
            cell.paragraphs.append(para)
            table_row.append(cell)
        table_node.rows.append(table_row)
    section.body.append(table_node)


def _build_list(section, node):
    """构建 HTML 列表"""
    items = node.get('items', [])
    for item_runs in items:
        para = ParagraphNode(alignment=Alignment.LEFT, line_spacing=LINE_SPACING, left_indent=20)
        para.add_run('• ', font_name=BODY_FONT, font_size=BODY_SIZE)
        for run in item_runs:
            text = run.get('text', '')
            if text:
                para.add_run(text, font_name=BODY_FONT, font_size=BODY_SIZE,
                             bold=run.get('style', {}).get('bold', False),
                             italic=run.get('style', {}).get('italic', False),
                             underline=run.get('style', {}).get('underline', False))
        section.body.append(para)


def _build_paragraph(section, node):
    """构建 HTML 段落/标题"""
    runs = node.get('runs', [])
    if not runs:
        return
    node_type = node.get('type', 'p')
    alignment = Alignment.CENTER if node_type in ('h1',) else Alignment.LEFT
    font_name = HEADING_FONT if node_type in ('h1', 'h2', 'h3', 'h4') else BODY_FONT
    para = ParagraphNode(alignment=alignment, line_spacing=LINE_SPACING)
    for run in runs:
        text = run.get('text', '')
        if text:
            para.add_run(text, font_name=font_name, font_size=BODY_SIZE,
                         bold=run.get('style', {}).get('bold', False),
                         italic=run.get('style', {}).get('italic', False),
                         underline=run.get('style', {}).get('underline', False))
    section.body.append(para)


def _build_md_list(section, node):
    """构建 Markdown 列表"""
    items = node.get('items', [])
    for j, item_runs in enumerate(items):
        prefix = f"{j + 1}. " if node.get('type') == 'ol' else "• "
        para = ParagraphNode(left_indent=32, first_line_indent=-16, line_spacing=LINE_SPACING,
                             space_after=0 if j < len(items) - 1 else 8)
        para.add_run(prefix, font_name=BODY_FONT, font_size=BODY_SIZE)
        for run_data in item_runs:
            text = run_data.get('text', '')
            if not text:
                continue
            run_style = run_data.get('style', {})
            para.add_run(text, font_name=run_style.get('font_name', BODY_FONT),
                         font_size=run_style.get('font_size', BODY_SIZE),
                         bold=run_style.get('bold', False),
                         italic=run_style.get('italic', False),
                         underline=run_style.get('underline', False),
                         color=run_style.get('color'),
                         background_color=run_style.get('background_color'))
        section.body.append(para)


def _build_md_paragraph(section, node):
    """构建 Markdown 段落/标题"""
    para_style = node.get('para_style', {})
    runs = node.get('runs', [])
    if not runs:
        return
    is_heading = para_style.get('is_heading', False)
    default_font = para_style.get('font_name', HEADING_FONT if is_heading else BODY_FONT)
    default_size = para_style.get('font_size', BODY_SIZE)
    align = (Alignment.CENTER if para_style.get('align') == 'center' else
             Alignment.RIGHT if para_style.get('align') == 'right' else Alignment.LEFT)
    space_after = 8 if is_heading else 0
    first_line_indent = 0 if is_heading else 32
    para = ParagraphNode(alignment=align, first_line_indent=first_line_indent,
                         line_spacing=LINE_SPACING, space_after=space_after)
    for run_data in runs:
        text = run_data.get('text', '')
        if not text:
            continue
        run_style = run_data.get('style', {})
        para.add_run(text, font_name=run_style.get('font_name', default_font),
                     font_size=run_style.get('font_size', default_size),
                     bold=run_style.get('bold', para_style.get('bold', False)),
                     italic=run_style.get('italic', para_style.get('italic', False)),
                     underline=run_style.get('underline', False),
                     color=run_style.get('color'),
                     background_color=run_style.get('background_color'))
    section.body.append(para)
