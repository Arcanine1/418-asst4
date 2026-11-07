#!/usr/bin/env python3
"""Compare the current workload estimate with the corrected candidate weighting."""
import compare

OLD = '''    } else
        return static_cast<unsigned long long>(number_of_possible_wires(wire)) * std::abs(wire.end_x - wire.start_x) +
               4 * std::abs(wire.end_y - wire.start_y) + 1;
    ;
'''
NEW = '''    } else {
        return static_cast<unsigned long long>(number_of_possible_wires(wire)) * wire_length_cost;
    }
'''
collective_variants = compare.variants


def work_variants(source):
    if source.count(OLD) != 1:
        raise ValueError('Work estimator changed; inspect it before rerunning this experiment.')
    fixed = source.replace(OLD, NEW, 1)
    return {'baseline': source, 'work_fixed': fixed,
            'work_fixed_allreduce': collective_variants(fixed)['allreduce']}


if __name__ == '__main__':
    compare.variants = work_variants
    compare.main()
