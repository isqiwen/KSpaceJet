from __future__ import annotations

import argparse
import pathlib

from .csv_io import observed_sizes
from .evaluation import evaluate_suite
from .execution import configure_cpu_affinity, default_sizes_for, run_one, selected_specs
from .models import BENCHMARKS, GateConfig
from .reporting import save_baseline, validate_baseline_compatibility, write_gate_json, write_report


def parse_module_sizes(specifications: list[str]) -> dict[str, str]:
    module_sizes: dict[str, str] = {}
    for specification in specifications:
        module, separator, values_text = specification.partition("=")
        module = module.strip()
        if separator != "=" or not module or not values_text.strip():
            raise ValueError(f"invalid --module-sizes value: {specification}; expected MODULE=A,B,C")
        if module not in BENCHMARKS:
            raise ValueError(f"unknown module in --module-sizes: {module}")
        if module in module_sizes:
            raise ValueError(f"duplicate --module-sizes entry for module: {module}")
        try:
            values = [int(value.strip()) for value in values_text.split(",")]
        except ValueError as error:
            raise ValueError(f"invalid sizes for module {module}: {values_text}") from error
        if not values or any(value <= 0 for value in values):
            raise ValueError(f"module sizes must be positive for {module}: {values_text}")
        module_sizes[module] = ",".join(str(value) for value in values)
    return module_sizes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run KSpaceJet numerics benchmark suite.")
    parser.add_argument(
        "--bin-dir",
        type=pathlib.Path,
        help="Directory containing benchmark binaries; required unless --evaluate-only is used.",
    )
    parser.add_argument("--out-dir", type=pathlib.Path, default=None, help="Output directory for CSV and reports.")
    parser.add_argument(
        "--evaluate-only",
        action="store_true",
        help="Re-evaluate existing module CSV files in --out-dir without executing benchmark binaries.",
    )
    parser.add_argument("--iterations", type=int, default=50, help="Minimum iterations in each benchmark trial.")
    parser.add_argument("--trials", type=int, default=5, help="Independent timing trials.")
    parser.add_argument(
        "--process-repetitions",
        type=int,
        default=None,
        help="Independent benchmark processes; defaults to 1 in smoke mode and 3 in policy mode.",
    )
    parser.add_argument(
        "--min-sample-time-us",
        type=int,
        default=1000,
        help="Minimum calibrated duration of each timing sample; --iterations remains the lower bound.",
    )
    parser.add_argument(
        "--cpu-affinity",
        default=None,
        help="Linux CPU list inherited by every benchmark process, for example 0 or 0,2-3.",
    )
    parser.add_argument(
        "--backend-threads",
        type=int,
        default=1,
        help="Threads allowed inside each numerical backend; defaults to 1 for serial policy measurements.",
    )
    parser.add_argument(
        "--module-sizes",
        action="append",
        default=[],
        metavar="MODULE=A,B,C",
        help="Module-specific size list; repeat for multiple modules, for example linalg=16,32,64.",
    )
    parser.add_argument("--only", nargs="*", choices=sorted(BENCHMARKS), default=None, help="Subset to run.")
    parser.add_argument(
        "--gate-mode",
        choices=("smoke", "policy"),
        default="smoke",
        help="Smoke fails correctness only; policy also fails confident policy and regression misses.",
    )
    parser.add_argument(
        "--min-speedup-percent",
        type=float,
        default=5.0,
        help="Minimum median improvement required to declare one backend meaningfully faster.",
    )
    parser.add_argument(
        "--max-policy-gap-percent",
        type=float,
        default=5.0,
        help="Largest confidently measured policy gap from the best candidate.",
    )
    parser.add_argument(
        "--baseline-dir",
        type=pathlib.Path,
        default=None,
        help="Prior suite output used as a performance regression baseline.",
    )
    parser.add_argument(
        "--max-regression-percent",
        type=float,
        default=10.0,
        help="Largest confidently measured median regression from --baseline-dir.",
    )
    parser.add_argument(
        "--save-baseline-dir",
        type=pathlib.Path,
        default=None,
        help="Copy this run's CSV, report, and gate metadata to a reusable baseline directory.",
    )
    parser.add_argument("--float-abs-tolerance", type=float, default=1.0e-4)
    parser.add_argument("--float-rel-tolerance", type=float, default=1.0e-5)
    parser.add_argument("--double-abs-tolerance", type=float, default=1.0e-10)
    parser.add_argument("--double-rel-tolerance", type=float, default=1.0e-10)
    return parser.parse_args()


def timestamp() -> str:
    from datetime import datetime

    return datetime.now().strftime("%Y%m%d-%H%M%S")


def main() -> int:
    args = parse_args()
    if args.evaluate_only and args.out_dir is None:
        raise ValueError("--evaluate-only requires --out-dir")
    if not args.evaluate_only and args.bin_dir is None:
        raise ValueError("--bin-dir is required unless --evaluate-only is used")
    process_repetitions = args.process_repetitions
    if process_repetitions is None:
        process_repetitions = 3 if args.gate_mode == "policy" else 1
    if (
        args.iterations <= 0
        or args.trials <= 0
        or process_repetitions <= 0
        or args.min_sample_time_us < 0
        or args.backend_threads <= 0
    ):
        raise ValueError(
            "--iterations, --trials, --process-repetitions, and --backend-threads must be positive; "
            "--min-sample-time-us must be non-negative"
        )
    cpu_affinity = configure_cpu_affinity(args.cpu_affinity)

    out_dir = args.out_dir or pathlib.Path("out/benchmarks/kspacejet-numerics-suite") / timestamp()
    out_dir.mkdir(parents=True, exist_ok=True)
    gate_config = GateConfig(
        mode=args.gate_mode,
        min_speedup_percent=args.min_speedup_percent,
        max_policy_gap_percent=args.max_policy_gap_percent,
        max_regression_percent=args.max_regression_percent,
        float_abs_tolerance=args.float_abs_tolerance,
        float_rel_tolerance=args.float_rel_tolerance,
        double_abs_tolerance=args.double_abs_tolerance,
        double_rel_tolerance=args.double_rel_tolerance,
    )
    measurement_config = {
        "minimum_iterations": args.iterations,
        "trials": args.trials,
        "minimum_sample_time_us": args.min_sample_time_us,
        "process_repetitions": process_repetitions,
        "backend_threads": args.backend_threads,
    }
    if args.baseline_dir is not None:
        validate_baseline_compatibility(args.baseline_dir, measurement_config, cpu_affinity)

    specs = selected_specs(args.only, BENCHMARKS)
    module_sizes = parse_module_sizes(args.module_sizes)
    selected_modules = {spec.name for spec in specs}
    unselected_modules = sorted(set(module_sizes) - selected_modules)
    if unselected_modules:
        raise ValueError("--module-sizes names not selected by --only: " + ", ".join(unselected_modules))

    csv_paths: dict[str, pathlib.Path] = {}
    sizes_by_name: dict[str, str] = {}
    for spec in specs:
        output = out_dir / spec.output
        if args.evaluate_only:
            if not output.exists():
                raise FileNotFoundError(f"missing benchmark CSV for --evaluate-only: {output}")
            sizes = module_sizes.get(spec.name, observed_sizes(output))
            csv_paths[spec.name] = output
        else:
            sizes = module_sizes.get(spec.name, default_sizes_for(spec))
            csv_paths[spec.name] = run_one(
                spec,
                args.bin_dir,
                out_dir,
                args.iterations,
                args.trials,
                args.min_sample_time_us,
                sizes,
                process_repetitions,
                args.backend_threads,
            )
        sizes_by_name[spec.name] = sizes

    issues, winners = evaluate_suite(csv_paths, args.baseline_dir, gate_config)
    report = write_report(
        csv_paths,
        sizes_by_name,
        out_dir,
        args.iterations,
        args.trials,
        process_repetitions,
        args.min_sample_time_us,
        gate_config,
        issues,
        winners,
        cpu_affinity,
        args.backend_threads,
    )
    write_gate_json(out_dir, gate_config, issues, winners, cpu_affinity, measurement_config)
    if args.save_baseline_dir is not None:
        save_baseline(
            out_dir,
            args.save_baseline_dir,
            csv_paths,
            gate_config,
            cpu_affinity,
            measurement_config,
        )

    print(report)
    return 1 if any(issue.status == "failure" for issue in issues) else 0
