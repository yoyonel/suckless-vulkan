#!/usr/bin/env python3
"""
Format gcovr JSON coverage report into a nice console table.
Displays: Filename | Lines | Covered | Cover% | Functions | Executed | Cover% | Branches | Covered | Cover%
"""

import json
import sys
from pathlib import Path
from typing import Any, Dict, List


def format_percent(value: float) -> str:
    """Format percentage with 2 decimal places."""
    return f"{value:.2f}%"


def humanize_filename(path: str, max_width: int = 40) -> str:
    """Shorten path for display."""
    if len(path) > max_width:
        parts = path.split('/')
        # Try to keep the interesting parts
        if len(parts) > 2:
            return "…/" + "/".join(parts[-2:])
    return path


def print_coverage_table(json_file: Path) -> None:
    """Parse and print coverage report as formatted table."""
    
    with open(json_file, 'r') as f:
        data = json.load(f)
    
    # Extract files and totals
    files = data.get('files', [])
    totals = data.get('gcovr/summary', {})
    
    # Table header
    col_widths = {
        'file': 40,
        'lines': 8,
        'cover': 8,
        'funcs': 8,
        'exec': 8,
        'branches': 10,
        'cover_br': 9,
    }
    
    print("\n" + "=" * 130)
    print("CODE COVERAGE REPORT - DETAILED BREAKDOWN")
    print("=" * 130)
    
    header = (
        f"{'Filename':<{col_widths['file']}} | "
        f"{'Lines':<{col_widths['lines']}} | "
        f"{'Cover':<{col_widths['cover']}} | "
        f"{'Functions':<{col_widths['funcs']}} | "
        f"{'Exec':<{col_widths['exec']}} | "
        f"{'Branches':<{col_widths['branches']}} | "
        f"{'B Cover':<{col_widths['cover_br']}}"
    )
    print(header)
    print("-" * 130)
    
    # Print each file
    for file_info in files:
        filename = file_info.get('file', 'unknown')
        
        # Lines coverage
        line_count = file_info.get('lines', {}).get('count', 0)
        line_covered = file_info.get('lines', {}).get('covered', 0)
        line_percent = (line_covered / line_count * 100) if line_count > 0 else 0
        
        # Function coverage
        func_count = file_info.get('functions', {}).get('count', 0)
        func_covered = file_info.get('functions', {}).get('covered', 0)
        func_percent = (func_covered / func_count * 100) if func_count > 0 else 0
        
        # Branch coverage
        branch_count = file_info.get('branches', {}).get('count', 0)
        branch_covered = file_info.get('branches', {}).get('covered', 0)
        branch_percent = (branch_covered / branch_count * 100) if branch_count > 0 else 0
        
        # Format line
        short_name = humanize_filename(filename, col_widths['file'])
        
        # Color coding for percentages (terminal colors)
        def color_percent(percent: float) -> str:
            if percent >= 80:
                return f"\033[92m{format_percent(percent)}\033[0m"  # Green
            elif percent >= 50:
                return f"\033[93m{format_percent(percent)}\033[0m"  # Yellow
            else:
                return f"\033[91m{format_percent(percent)}\033[0m"  # Red
        
        line = (
            f"{short_name:<{col_widths['file']}} | "
            f"{line_count:<{col_widths['lines']}} | "
            f"{color_percent(line_percent):<{col_widths['cover']+10}} | "
            f"{func_count:<{col_widths['funcs']}} | "
            f"{color_percent(func_percent):<{col_widths['exec']+10}} | "
            f"{branch_count:<{col_widths['branches']}} | "
            f"{color_percent(branch_percent):<{col_widths['cover_br']+10}}"
        )
        print(line)
    
    # Print totals
    print("-" * 130)
    
    total_lines = totals.get('lines', {}).get('count', 0)
    total_lines_covered = totals.get('lines', {}).get('covered', 0)
    total_lines_percent = (total_lines_covered / total_lines * 100) if total_lines > 0 else 0
    
    total_funcs = totals.get('functions', {}).get('count', 0)
    total_funcs_covered = totals.get('functions', {}).get('covered', 0)
    total_funcs_percent = (total_funcs_covered / total_funcs * 100) if total_funcs > 0 else 0
    
    total_branches = totals.get('branches', {}).get('count', 0)
    total_branches_covered = totals.get('branches', {}).get('covered', 0)
    total_branches_percent = (total_branches_covered / total_branches * 100) if total_branches > 0 else 0
    
    total_line = (
        f"{'TOTAL':<{col_widths['file']}} | "
        f"{total_lines:<{col_widths['lines']}} | "
        f"{color_percent(total_lines_percent):<{col_widths['cover']+10}} | "
        f"{total_funcs:<{col_widths['funcs']}} | "
        f"{color_percent(total_funcs_percent):<{col_widths['exec']+10}} | "
        f"{total_branches:<{col_widths['branches']}} | "
        f"{color_percent(total_branches_percent):<{col_widths['cover_br']+10}}"
    )
    print(total_line)
    print("=" * 130)
    
    # Print summary
    print("\n📊 SUMMARY:")
    print(f"  • Overall Line Coverage:      {color_percent(total_lines_percent)}")
    print(f"  • Overall Function Coverage:  {color_percent(total_funcs_percent)}")
    print(f"  • Overall Branch Coverage:    {color_percent(total_branches_percent)}")
    print(f"  • Total Lines:                {total_lines_covered}/{total_lines}")
    print(f"  • Total Functions:            {total_funcs_covered}/{total_funcs}")
    print(f"  • Total Branches:             {total_branches_covered}/{total_branches}")
    print()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 format_coverage_report.py <coverage.json>")
        sys.exit(1)
    
    json_file = Path(sys.argv[1])
    if not json_file.exists():
        print(f"Error: {json_file} not found")
        sys.exit(1)
    
    print_coverage_table(json_file)
