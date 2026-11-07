#!/usr/bin/env python3
"""Measure the user's uniform splitting change against the serial exception."""
import compare

UNIFORM = '''        unsigned int splits =
            std::min<unsigned long long>(candidates, wire_work[i] ? 1 + (wire_work[i] - 1) / target : 1);'''
SPECIAL = '''        unsigned int splits =
            nproc == 1 ? 1
                       : std::min<unsigned long long>(candidates, wire_work[i] ? 1 + (wire_work[i] - 1) / target : 1);'''


def split_variants(source):
    if source.count(UNIFORM) != 1:
        raise ValueError('Expected your edited source with the single-rank splitting exception removed.')
    return {'serial_exception': source.replace(UNIFORM, SPECIAL, 1), 'uniform_split': source}


if __name__ == '__main__':
    compare.variants = split_variants
    compare.INPUT_REFERENCES = {'few': 29240, 'medium': 267589, 'abundant': 1539198}
    compare.RUN_CONFIGS = ((1, 1, 3), (16, 1, 3), (16, 128, 7))
    compare.main()
