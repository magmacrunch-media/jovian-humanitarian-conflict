"""``python -m jovian`` — fly the rail in a terminal."""

from __future__ import annotations

import argparse


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="jovian",
        description="The Jovian Humanitarian Conflict - terminal version.",
    )
    parser.add_argument(
        "--seed", type=int, default=None,
        help="fix the run, for a reproducible one",
    )
    parser.add_argument(
        "--play", action="store_true",
        help="skip the title screen and fly",
    )
    args = parser.parse_args()

    # Imported here, not at module scope, so --help works without the engine
    # or its terminal extra installed.
    from jovian.app import run

    run(args.seed, args.play)


if __name__ == "__main__":
    main()
