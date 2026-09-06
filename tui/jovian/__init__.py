"""The Jovian Humanitarian Conflict — terminal version.

An on-rails shooter over the cloud decks of Jupiter. Aid convoys fly among the
hostiles and squawk a transponder you must learn to read; shoot one and it
costs you the run.

The rules are a port of ``web/js/``, which stays the source of truth. See
``jovian/config.py`` for the constants and why they are the numbers they are.
"""

# Kept in step with ``pyproject.toml`` by hand, the way the other cabinets do
# it: a literal answers the same in a source checkout as in an installed wheel,
# which asking importlib.metadata would not. tests/test_packaging.py is what
# stops the two drifting.
__version__ = "0.1.0"
