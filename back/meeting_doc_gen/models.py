#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""会议文档生成器 - 数据结构定义"""

from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Union


class Alignment(Enum):
    LEFT = 'left'
    CENTER = 'center'
    RIGHT = 'right'
    JUSTIFY = 'justify'


@dataclass
class RunNode:
    """文本块节点 - 最小格式单元"""
    text: str = ''
    font_name: str = '仿宋'
    font_name_ascii: str = 'Times New Roman'
    font_size: float = 16
    bold: bool = False
    italic: bool = False
    underline: bool = False
    color: Optional[str] = None
    background_color: Optional[str] = None
    link: Optional[str] = None


@dataclass
class ParagraphNode:
    """段落节点"""
    runs: List[RunNode] = field(default_factory=list)
    alignment: Alignment = Alignment.LEFT
    first_line_indent: float = 0
    left_indent: float = 0
    line_spacing: float = 1.5
    space_before: float = 0
    space_after: float = 0

    def add_run(self, text: str, **style) -> 'ParagraphNode':
        self.runs.append(RunNode(text=text, **style))
        return self


@dataclass
class TableCell:
    """表格单元格"""
    paragraphs: List[ParagraphNode] = field(default_factory=list)


@dataclass
class TableNode:
    """表格节点"""
    rows: List[List[TableCell]] = field(default_factory=list)


@dataclass
class SectionNode:
    """版面节点 - 控制页面尺寸（符合 GB/T 9704-2012）"""
    body: List[Union[ParagraphNode, TableNode]] = field(default_factory=list)
    page_width: float = 21.0       # A4 宽度 210mm
    page_height: float = 29.7      # A4 高度 297mm
    margin_top: float = 3.7        # 天头 37mm
    margin_bottom: float = 3.5     # 下白边
    margin_left: float = 2.8       # 订口 28mm
    margin_right: float = 2.6      # 右白边


@dataclass
class DocumentNode:
    """文档节点 - 顶层结构"""
    sections: List[SectionNode] = field(default_factory=list)

    def add_section(self) -> SectionNode:
        section = SectionNode()
        self.sections.append(section)
        return section


@dataclass
class ListItem:
    """列表项"""
    content: str = ''
    runs: List[RunNode] = field(default_factory=list)
    items: List['ListItem'] = field(default_factory=list)
