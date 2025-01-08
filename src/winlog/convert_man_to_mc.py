# SPDX-FileCopyrightText: 2025 Kreijstal
# 
# SPDX-License-Identifier: Apache-2.0

import xml.etree.ElementTree as ET
import argparse
import sys
import os

# Define the namespaces
namespaces = {
    'ns': 'http://schemas.microsoft.com/win/2004/08/events',
    'win': 'http://manifests.microsoft.com/win/2004/08/windows/events',
    'xs': 'http://www.w3.org/2001/XMLSchema'
}

def main(input_file, output_file):
    # Check if the input file has a .man extension
    if not input_file.endswith('.man'):
        print("Error: Input file must have a .man extension.")
        sys.exit(1)

    # Parse the .man file
    try:
        tree = ET.parse(input_file)
    except ET.ParseError as e:
        print(f"Error: Failed to parse the XML file: {e}")
        sys.exit(1)
    except FileNotFoundError:
        print(f"Error: File '{input_file}' not found.")
        sys.exit(1)

    root = tree.getroot()

    # Extract provider information
    provider = root.find('.//ns:provider', namespaces)
    if provider is None:
        print("Error: Could not find the <provider> element in the XML.")
        sys.exit(1)

    provider_name = provider.get('name')
    provider_guid = provider.get('guid')
    provider_symbol = provider.get('symbol')

    # Convert provider name to a valid function name (e.g., "Pistache-Provider" -> "Pistache_Provider")
    provider_func_name = provider_name.replace('-', '_')

    # Extract events
    events = provider.find('ns:events', namespaces).findall('ns:event', namespaces)

    # Extract localization strings
    strings = {}
    string_table = root.find('.//ns:stringTable', namespaces).findall('ns:string', namespaces)
    for string in string_table:
        strings[string.get('id')] = string.get('value')

    # Start building the .mc content
    mc_content = 'MessageIdTypedef=DWORD\n\n'

    # Provider Information
    mc_content += f';// Provider Information\n'
    mc_content += f';// Name: {provider_name}\n'
    mc_content += f';// GUID: {provider_guid}\n'
    mc_content += f';// Symbol: {provider_symbol}\n\n'

    # Events
    mc_content += ';// Events\n'
    for event in events:
        message_id = event.get('value')
        symbolic_name = event.get('symbol')
        message = event.get('message')
        if message.startswith('$(string.Event.'):
            message_id_str = message.lstrip('$(string.Event.').rstrip(')')
            message_text = strings.get(f'Event.{message_id_str}', '')
        else:
            message_text = message
        mc_content += f'MessageId={message_id}\n'
        mc_content += f'SymbolicName={symbolic_name}\n'
        mc_content += 'Language=English\n'
        mc_content += f'{message_text}\n.\n\n'
        # Add comments for channel, level, task, and template
        channel = event.get('channel')
        level = event.get('level')
        task = event.get('task')
        template = event.get('template')
        mc_content += f';// Channel: {channel}\n'
        mc_content += f';// Level: {level}\n'
        mc_content += f';// Task: {task}\n'
        mc_content += f';// Template: {template}\n\n'

    # Generate Event Descriptors
    mc_content += ';// Event Descriptors\n'
    for event in events:
        message_id = event.get('value')
        symbolic_name = event.get('symbol')
        mc_content += f';static const EVENT_DESCRIPTOR EventDesc_{symbolic_name} = {{ {message_id}, 1, 0, 0, 0, 0, 0 }};\n'
    mc_content += '\n'

    # Generate Provider Handle and GUID
    mc_content += ';// Provider Handle and GUID\n'
    mc_content += f';static REGHANDLE {provider_func_name}Handle = 0;\n'

    # Format the GUID correctly
    guid_parts = provider_guid.strip('{}').split('-')
    guid_hex = [
        f'0x{guid_parts[0]}',  # Data1
        f'0x{guid_parts[1]}',  # Data2
        f'0x{guid_parts[2]}',  # Data3
        f'0x{guid_parts[3][0:2]}',  # Data4[0]
        f'0x{guid_parts[3][2:4]}',  # Data4[1]
        f'0x{guid_parts[4][0:2]}',  # Data4[2]
        f'0x{guid_parts[4][2:4]}',  # Data4[3]
        f'0x{guid_parts[4][4:6]}',  # Data4[4]
        f'0x{guid_parts[4][6:8]}',  # Data4[5]
        f'0x{guid_parts[4][8:10]}',  # Data4[6]
        f'0x{guid_parts[4][10:12]}'  # Data4[7]
    ]
    mc_content += f';static const GUID {provider_symbol} =\n'
    mc_content += f';{{ {guid_hex[0]}, {guid_hex[1]}, {guid_hex[2]}, {{ {guid_hex[3]}, {guid_hex[4]}, {guid_hex[5]}, {guid_hex[6]}, {guid_hex[7]}, {guid_hex[8]}, {guid_hex[9]}, {guid_hex[10]} }} }};\n\n'

    # Generate Provider Registration Code
    mc_content += ';// Provider Registration and Unregistration\n'
    mc_content += f';static inline ULONG EventRegister{provider_func_name}() {{\n'
    mc_content += f';    return EventRegister(&{provider_symbol}, nullptr, nullptr, &{provider_func_name}Handle);\n'
    mc_content += ';}\n\n'
    mc_content += f';static inline ULONG EventUnregister{provider_func_name}() {{\n'
    mc_content += f';    return EventUnregister({provider_func_name}Handle);\n'
    mc_content += ';}\n\n'

    # Generate Event Writing Functions
    mc_content += ';// Event Writing Functions\n'
    mc_content += ';#define GENERATE_EVENT_WRITE_FUNCTION(event_name, event_descriptor) \\\n'
    mc_content += ';    static inline ULONG event_name(PCWSTR message) { \\\n'
    mc_content += ';        EVENT_DATA_DESCRIPTOR descriptor; \\\n'
    mc_content += ';        EventDataDescCreate(&descriptor, message, (ULONG)((wcslen(message) + 1) * sizeof(WCHAR))); \\\n'
    mc_content += f';        return EventWrite({provider_func_name}Handle, &event_descriptor, 1, &descriptor); \\\n'
    mc_content += ';    }\n\n'

    # Generate AssumeEnabled Macros
    mc_content += ';// AssumeEnabled Macros\n'
    mc_content += ';#define GENERATE_ASSUME_ENABLED_MACRO(event_name) \\\n'
    mc_content += ';    static inline ULONG event_name##_AssumeEnabled(PCWSTR message) { \\\n'
    mc_content += ';        return event_name(message); \\\n'
    mc_content += ';    }\n\n'

    # Generate Functions for Each Event
    for event in events:
        symbolic_name = event.get('symbol')
        mc_content += f';GENERATE_EVENT_WRITE_FUNCTION(EventWrite{symbolic_name}, EventDesc_{symbolic_name})\n'
        mc_content += f';GENERATE_ASSUME_ENABLED_MACRO(EventWrite{symbolic_name})\n\n'

    # Write to .mc file
    try:
        with open(output_file, 'w', encoding='utf-8') as mc_file:
            mc_file.write(mc_content)
        print(f"Successfully wrote to {output_file}")
    except IOError as e:
        print(f"Error: Failed to write to {output_file}: {e}")
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert a .man file to a .mc file.")
    parser.add_argument("input_file", help="Path to the input .man file")
    parser.add_argument("output_file", help="Path to the output .mc file")
    args = parser.parse_args()

    main(args.input_file, args.output_file)
