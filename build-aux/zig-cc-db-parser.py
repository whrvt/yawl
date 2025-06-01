#!/usr/bin/env python3
"""
Script to generate a compile_commands.json database from compiler invocations.
Takes an input file containing commands of the format:
zig clang <source_file> <flags> -MT <target> ... -c -o <output_file>
- Claude 3.7 Sonnet
"""

import json
import sys
import os
import argparse


def parse_command(command, base_directory):
    """Parse a compiler command and extract relevant information."""
    # Skip empty lines or comments
    command = command.strip()
    if not command or command.startswith('#'):
        return None

    parts = command.split()

    # Check if this is a zig clang command
    if len(parts) < 3 or not parts[0].__contains__("zig") or parts[1] != "clang":
        return None

    # Source file is the 3rd element (index 2)
    source_file = parts[2]

    # Find -MT index
    mt_index = -1
    for i, part in enumerate(parts):
        if part == "-MT":
            mt_index = i
            break

    if mt_index == -1:
        # If there's no -MT, skip this line
        return None

    # Extract flags between source file and -MT (excluding both)
    flags = parts[3:mt_index]

    # Find output file (after -o)
    output_file = None
    for i, part in enumerate(parts):
        if part == "-o" and i + 1 < len(parts):
            output_file = parts[i + 1]
            break

    if not output_file:
        return None

    # Construct the JSON object
    result = {
        "arguments": ["clang++"] + flags + ["-c", "-o", output_file, source_file],
        "directory": base_directory,
        "file": os.path.join(base_directory, source_file),
        "output": os.path.join(base_directory, output_file)
    }

    return result


def main():
    """Main function."""
    parser = argparse.ArgumentParser(
        description='Generate compile_commands.json from compiler invocations')
    parser.add_argument('input_file', help='File containing compiler commands')
    parser.add_argument('--directory', '-d', default=os.getcwd(),
                        help='Base directory for compilation (default: current directory)')
    parser.add_argument('--output', '-o', default='compile_commands.json',
                        help='Output JSON file (default: compile_commands.json)')

    args = parser.parse_args()

    try:
        with open(args.input_file, 'r') as f:
            commands = f.readlines()
    except Exception as e:
        print(f"Error reading input file: {e}")
        sys.exit(1)

    # Parse commands
    results = []
    for i, command in enumerate(commands, 1):
        try:
            result = parse_command(command, args.directory)
            if result:
                results.append(result)
        except Exception as e:
            print(f"Error parsing line {i}: {e}")
            print(f"Line content: {command.strip()}")

    if not results:
        print("Warning: No valid commands found in the input file")

    # Write JSON output
    try:
        with open(args.output, 'w') as f:
            json.dump(results, f, indent=2)
        print(
            f"Successfully created {args.output} with {len(results)} entries")
    except Exception as e:
        print(f"Error writing output file: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
