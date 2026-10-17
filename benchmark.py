#!/usr/bin/env python3

import argparse
import csv
import re
import subprocess
import sys


test_mse = re.compile(r";;- Test MSE: (\S+)")
test_accuracy = re.compile(r";;- Test accuracy: (\S+)")
rgb_mse = re.compile(r";; RGB averaged test MSE \(before display clamping\): (\S+)")
timer = re.compile(r";;Time\(.*\) = ([0-9.]+) sec")

workloads = {
    "diabetes": test_mse,
    "housing": test_mse,
    "cancer": test_accuracy,
    "silk-wave": rgb_mse,
    "square-wave": rgb_mse,
}


def search(pattern, output, workload, seed):
    match = pattern.search(output)
    if not match:
        sys.exit("%s seed %d: no match for %s" % (workload, seed, pattern.pattern))
    return float(match.group(1))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("id")
    parser.add_argument("--workloads", nargs="+", choices=list(workloads),
                        default=["diabetes"])
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--output")
    args = parser.parse_args()

    rows = []
    for workload in args.workloads:
        for run in range(1, args.runs + 1):
            seed = run * 1000000009 % 2**32
            output = subprocess.run(
                ["./genetic_benchmark", workload, str(seed)],
                check=True, text=True, stdout=subprocess.PIPE
            ).stdout
            time = search(timer, output, workload, seed)
            quality = search(workloads[workload], output, workload, seed)
            print("%s %d/%d %.6f" % (workload, run, args.runs, time), file=sys.stderr)
            rows.append([args.id, workload, seed, time, quality])

    file = open(args.output, "a", newline="") if args.output else sys.stdout
    writer = csv.writer(file)
    if file is sys.stdout or file.tell() == 0:
        writer.writerow(["commit", "workload", "seed", "time", "quality"])
    writer.writerows(rows)


if __name__ == "__main__":
    main()
