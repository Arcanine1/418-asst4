#!/usr/bin/env python3
"""Refine the measured vertical-work weight around the winning factor two."""
import compare


def fine_variants(source):
    start = source.index('unsigned long long work_of_wire(')
    end = source.index('// Returns wires', start)
    function = source[start:end]
    corrected = 'static_cast<unsigned long long>(number_of_possible_wires(wire)) * wire_length_cost'
    vertical = '2 * std::abs(wire.end_y - wire.start_y)'
    if corrected not in function or function.count(vertical) != 1:
        raise ValueError('Copy the corrected production source with vertical weight 2 first.')
    return {f'weight{weight}': source[:start] + function.replace(
                vertical, f'{weight} * std::abs(wire.end_y - wire.start_y)') + source[end:]
            for weight in (2, 1, 3)}


if __name__ == '__main__':
    compare.variants = fine_variants
    compare.INPUT_REFERENCES = {'few': 29240, 'medium': 267589, 'abundant': 1539198}
    compare.main()
