#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""会议文档生成器 - HTML / Markdown 解析器"""

import json
import re
from typing import Any, Dict, List, Optional

from bs4 import BeautifulSoup, NavigableString
from loguru import logger


def is_html_content(content: str) -> bool:
    """检测内容是否为 HTML 格式"""
    if not content:
        return False
    html_tags = ['<h1', '<h2', '<h3', '<h4', '<p>', '<ul>', '<ol>', '<li>', '<div', '<br']
    return any(tag in content.lower() for tag in html_tags)


def is_editorjs_format(content: str) -> bool:
    """检测内容是否为 Editor.js JSON 格式"""
    content = content.strip()
    if not content.startswith('{'):
        return False
    try:
        data = json.loads(content)
        if 'blocks' in data and isinstance(data.get('blocks'), list):
            return True
    except json.JSONDecodeError:
        pass
    return False


# ---------------------------------------------------------------------------
# HTML 解析
# ---------------------------------------------------------------------------

def _parse_span_style(element) -> Dict[str, Any]:
    """解析 span 元素的 style 属性"""
    style = {}
    style_attr = element.get('style', '')
    if 'font-weight: bold' in style_attr or 'font-weight:bold' in style_attr:
        style['bold'] = True
    if 'font-style: italic' in style_attr or 'font-style:italic' in style_attr:
        style['italic'] = True
    if 'text-decoration: underline' in style_attr or 'text-decoration:underline' in style_attr:
        style['underline'] = True
    font_size_match = re.search(r'font-size:\s*(\d+)px', style_attr)
    if font_size_match:
        style['font_size'] = int(font_size_match.group(1)) * 0.75
    font_family_match = re.search(r'font-family:\s*([^;]+)', style_attr)
    if font_family_match:
        style['font_name'] = font_family_match.group(1).strip().strip('"').strip("'")
    color_match = re.search(r'(?<!background-)color:\s*#([0-9a-fA-F]{6})', style_attr)
    if color_match:
        style['color'] = color_match.group(1)
    bg_color_match = re.search(r'background-color:\s*#([0-9a-fA-F]{6})', style_attr)
    if bg_color_match:
        style['background_color'] = bg_color_match.group(1)
    return style


def _parse_inline_elements(element, inherited_style: Optional[Dict] = None) -> List[Dict[str, Any]]:
    """递归解析内联元素，拆分成多个 run"""
    runs = []
    inherited_style = inherited_style or {}
    for child in element.children:
        if isinstance(child, NavigableString):
            text = str(child)
            if text.strip() or text:
                runs.append({'text': text, 'style': dict(inherited_style)})
        elif child.name:
            child_style = dict(inherited_style)
            tag = child.name.lower()
            if tag in ('strong', 'b'):
                child_style['bold'] = True
            elif tag in ('em', 'i'):
                child_style['italic'] = True
            elif tag == 'u':
                child_style['underline'] = True
            elif tag == 'span':
                child_style.update(_parse_span_style(child))
            runs.extend(_parse_inline_elements(child, child_style))
    return runs


def _parse_para_style(element) -> Dict[str, Any]:
    """解析段落级样式"""
    style = {}
    style_attr = element.get('style', '')
    if 'text-align: center' in style_attr or 'text-align:center' in style_attr:
        style['align'] = 'center'
    elif 'text-align: right' in style_attr or 'text-align:right' in style_attr:
        style['align'] = 'right'
    return style


def _parse_html_element(element) -> Optional[Dict[str, Any]]:
    """解析单个 HTML 元素"""
    if element.name is None:
        text = str(element).strip()
        return {'type': 'p', 'runs': [{'text': text, 'style': {}}]} if text else None

    tag = element.name.lower()

    # 列表
    if tag in ('ul', 'ol'):
        items = []
        for li in element.find_all('li', recursive=False):
            runs = _parse_inline_elements(li)
            if runs:
                items.append(runs)
        return {'type': tag, 'items': items}

    # 表格
    if tag == 'table':
        rows = []
        for tr in element.find_all('tr'):
            cells = []
            for cell in tr.find_all(['th', 'td']):
                cell_runs = _parse_inline_elements(cell)
                cell_text = ''.join([r['text'] for r in cell_runs]) if cell_runs else ''
                cells.append({
                    'text': cell_text.strip(),
                    'is_header': cell.name == 'th',
                    'colspan': int(cell.get('colspan', 1)),
                    'rowspan': int(cell.get('rowspan', 1))
                })
            if cells:
                rows.append(cells)
        if rows:
            return {'type': 'table', 'rows': rows}
        return None

    para_style = _parse_para_style(element)

    heading_styles = {
        'h1': {'font_size': 22, 'bold': False, 'font_name': '方正小标宋简体', 'align': 'center', 'is_heading': True},
        'h2': {'font_size': 16, 'bold': False, 'font_name': '黑体', 'is_heading': True},
        'h3': {'font_size': 16, 'bold': False, 'font_name': '楷体', 'is_heading': True},
        'h4': {'font_size': 16, 'bold': False, 'font_name': '仿宋', 'is_heading': True},
    }
    if tag in heading_styles:
        para_style.update(heading_styles[tag])

    runs = _parse_inline_elements(element)
    if not runs:
        return None

    node_type = tag if tag in heading_styles else 'p'
    return {'type': node_type, 'runs': runs, 'para_style': para_style}


def parse_html(html_content: str) -> List[Dict[str, Any]]:
    """解析 HTML 富文本为节点列表"""
    try:
        html_content = html_content.replace('<br>', '</p><p>').replace('<br/>', '</p><p>').replace('<br />', '</p><p>')
        soup = BeautifulSoup(html_content, 'html.parser')
        nodes = []
        for element in soup.children:
            node = _parse_html_element(element)
            if node:
                nodes.append(node)
        return nodes
    except Exception as e:
        logger.error(f"解析 HTML 失败: {e}")
        return []


# ---------------------------------------------------------------------------
# Markdown 解析
# ---------------------------------------------------------------------------

def _preprocess_markdown(md_content: str) -> str:
    """预处理非标准 Markdown 格式"""
    processed_md = re.sub(r'(#+)([^#\s])', r'\1 \2', md_content)
    processed_md = re.sub(r'([^\n])([•\-*]\s)', r'\1\n\2', processed_md)
    processed_md = re.sub(r'([^\s])\*\*(\S)', r'\1 **\2', processed_md)
    processed_md = re.sub(r'(\S)\*\*([^\s])', r'\1** \2', processed_md)
    processed_md = re.sub(r'^--\s*$', '---', processed_md, flags=re.MULTILINE)
    processed_md = re.sub(r'^\s*(#+\s)', r'\1', processed_md, flags=re.MULTILINE)
    processed_md = re.sub(r'\n{3,}', r'\n\n', processed_md)
    return processed_md


def parse_markdown(md_content: str) -> List[Dict[str, Any]]:
    """解析 Markdown 为节点列表"""
    try:
        import markdown
        processed_md = _preprocess_markdown(md_content)
        html_content = markdown.markdown(processed_md)
        return parse_html(html_content)
    except Exception as e:
        logger.error(f"解析 Markdown 失败: {e}")
        return []
