#!/usr/bin/env python3
"""Test vertical-work estimates on all inputs, using the corrected formula."""
import compare


def weight_variants(source):
    start = source.index('unsigned long long work_of_wire(')
    end = source.index('// Returns wires', start)
    function = source[start:end]
    corrected = 'static_cast<unsigned long long>(number_of_possible_wires(wire)) * wire_length_cost'
    if corrected not in function:
        raise ValueError('Copy the corrected production source before running this experiment.')
    vertical = '4 * std::abs(wire.end_y - wire.start_y)'
    if function.count(vertical) != 1:
        raise ValueError('Work estimator changed; inspect it before generating variants.')
    return {
        'weight4': source,
        'weight2': source[:start] + function.replace(vertical, vertical.replace('4 *', '2 *', 1)) + source[end:],
        'weight8': source[:start] + function.replace(vertical, vertical.replace('4 *', '8 *', 1)) + source[end:],
    }


if __name__ == '__main__':
    compare.variants = weight_variants
    compare.INPUT_REFERENCES = {'few': 29240, 'medium': 267589, 'abundant': 1539198}
    compare.main()
