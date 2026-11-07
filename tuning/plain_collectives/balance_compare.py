#!/usr/bin/env python3
"""Test measured work weights, then finer partitioning, with plain MPI."""
import compare


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('Source changed; expected exactly one occurrence of: ' + old)
    return source.replace(old, new, 1)


def balance_variants(source):
    weighted = replace_once(source,
        'unsigned long long work_of_wire(const Wire& wire, bool is_random)',
        'unsigned long long work_of_wire(const Wire& wire, bool is_random, unsigned int vertical_weight)')
    weighted = replace_once(weighted, '2 * std::abs(wire.end_y - wire.start_y)',
                            'vertical_weight * std::abs(wire.end_y - wire.start_y)')
    weighted = replace_once(weighted, 'work_of_wire(wires[i], is_random)',
                            'work_of_wire(wires[i], is_random, num_wires <= 512 ? 2 : 1)')
    finer = replace_once(weighted,
        'total_work / (static_cast<unsigned long long>(partition_multiplier) * nproc)',
        'total_work / (static_cast<unsigned long long>(partition_multiplier) * nproc *\n'
        '                               (nproc >= 64 && num_wires >= 1024 ? 2ULL : 1ULL))')
    return {'weight2': source, 'size_weight': weighted, 'finer': finer}


if __name__ == '__main__':
    compare.variants = balance_variants
    compare.INPUT_REFERENCES = {'few': 29240, 'medium': 267589, 'abundant': 1539198}
    compare.main()
