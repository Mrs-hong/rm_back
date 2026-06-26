#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""会议文档生成器 - 配置与格式化工具"""

import re
from datetime import datetime
from typing import Any, Callable, Dict

from .models import Alignment

# =============================================================================
# GB/T 9704-2012 标准配置
# =============================================================================

GB_STANDARD = {
    'page_width': 21.0,
    'page_height': 29.7,
    'margin_top': 3.7,
    'margin_bottom': 3.5,
    'margin_left': 2.8,
    'margin_right': 2.6,
    'content_width': 15.6,
    'content_height': 22.5,
    'lines_per_page': 22,
    'chars_per_line': 28,
    'font_3': 16,
    'font_4': 14,
    'font_2': 22,
    'line_height_3': 16 + 16 * 7 / 8,
}

DEFAULT_STYLES: Dict[str, Dict] = {
    '标题': {
        'font_name': '方正小标宋简体', 'font_name_ascii': 'Times New Roman',
        'font_size': 22, 'bold': False,
        'first_line_indent': 0, 'left_indent': 0,
        'line_spacing': 1.0, 'space_before': 0, 'space_after': 0,
        'alignment': Alignment.CENTER,
    },
    '一级标题': {
        'font_name': '黑体', 'font_name_ascii': 'Times New Roman',
        'font_size': 16, 'bold': False,
        'first_line_indent': 0, 'left_indent': 0,
        'line_spacing': 1.0, 'space_before': 0, 'space_after': 0,
        'alignment': Alignment.LEFT,
    },
    '二级标题': {
        'font_name': '楷体', 'font_name_ascii': 'Times New Roman',
        'font_size': 16, 'bold': False,
        'first_line_indent': 0, 'left_indent': 0,
        'line_spacing': 1.0, 'space_before': 0, 'space_after': 0,
        'alignment': Alignment.LEFT,
    },
    '三级标题': {
        'font_name': '仿宋', 'font_name_ascii': 'Times New Roman',
        'font_size': 16, 'bold': False,
        'first_line_indent': 0, 'left_indent': 0,
        'line_spacing': 1.0, 'space_before': 0, 'space_after': 0,
        'alignment': Alignment.LEFT,
    },
    '正文': {
        'font_name': '仿宋', 'font_name_ascii': 'Times New Roman',
        'font_size': 16, 'bold': False,
        'first_line_indent': 32, 'left_indent': 0,
        'line_spacing': 1.0, 'space_before': 0, 'space_after': 0,
        'alignment': Alignment.JUSTIFY,
    },
}

DEFAULT_PATTERNS = {
    '一级标题': re.compile(r'^[一二三四五六七八九十]+[、．.]'),
    '二级标题': re.compile(r'^[（(][一二三四五六七八九十]+[）)]'),
    '三级标题': re.compile(r'^\d+[、．.\s]'),
    '四级标题': re.compile(r'^[（(]\d+[）)]|^[①②③④⑤⑥⑦⑧⑨⑩]'),
}

# 会议类型映射
KIND_LABEL_MAP: Dict[int, str] = {
    0: '标准会议',
    1: '党委会议',
    2: '决策会议',
    3: '晨会',
}

# 数据映射表
MEETING_MAPPING = {
    '会议主题': ('theme', 'default'),
    '会议时间': ('recording_time', 'datetime'),
    '会议地点': ('places', 'or_none'),
    '主持人': ('moderator', 'or_none'),
    '记录人': ('recorder', 'or_none'),
    '参会人员': ('attendees', 'list'),
    '会议类型': ('kind', 'kind_label'),
    '正文': ('content', 'default'),
    '备注': ('remark', 'or_none'),
}

NOTE_MAPPING = {
    '标题': ('title', 'default'),
    '创建时间': ('create_time', 'datetime'),
    '内容': ('content', 'default'),
}

# =============================================================================
# 格式化工具
# =============================================================================

def _fmt_date(v: Any) -> str:
    """格式化日期（GB/T 9704-2012）"""
    if not v:
        return ''
    if isinstance(v, str):
        return v
    if isinstance(v, datetime):
        return f"{v.year}年{v.month}月{v.day}日"
    if isinstance(v, (int, float)):
        try:
            dt = datetime.fromtimestamp(v / 1000)
            return f"{dt.year}年{dt.month}月{dt.day}日"
        except (OSError, OverflowError, ValueError):
            return str(v)
    return str(v)


def _fmt_datetime(v: Any) -> str:
    """格式化日期时间"""
    if not v:
        return ''
    if isinstance(v, str):
        return v
    if isinstance(v, datetime):
        return f"{v.year}年{v.month}月{v.day}日 {v.hour:02d}:{v.minute:02d}"
    if isinstance(v, (int, float)):
        try:
            dt = datetime.fromtimestamp(v / 1000)
            return f"{dt.year}年{dt.month}月{dt.day}日 {dt.hour:02d}:{dt.minute:02d}"
        except (OSError, OverflowError, ValueError):
            return str(v)
    return str(v)


FORMATTERS: Dict[str, Callable] = {
    'default': lambda v: str(v) if v is not None else '',
    'date': _fmt_date,
    'datetime': _fmt_datetime,
    'list': lambda v: '、'.join(str(i) for i in v) if isinstance(v, list) else str(v or ''),
    'or_none': lambda v: str(v) if v and not (isinstance(v, list) and len(v) == 0) else '无',
    'list_or_none': lambda v: '、'.join(str(i) for i in v) if v and isinstance(v, list) else '无',
    'kind_label': lambda v: KIND_LABEL_MAP.get(v, '标准会议') if isinstance(v, int) else str(v or '标准会议'),
}


def format_value(value: Any, formatter: str = 'default') -> str:
    """根据格式化器名称格式化值"""
    func = FORMATTERS.get(formatter, FORMATTERS['default'])
    try:
        return func(value)
    except Exception:
        return str(value) if value is not None else ''


def apply_mapping(data: Dict[str, Any], mapping: Dict) -> Dict[str, str]:
    """应用映射表，将业务数据转换为占位符内容"""
    result = {}
    for placeholder, field_spec in mapping.items():
        if isinstance(field_spec, tuple):
            field_name, formatter = field_spec
        else:
            field_name, formatter = field_spec, 'default'
        result[placeholder] = format_value(data.get(field_name), formatter)
    return result
