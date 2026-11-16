"""figtools training helpers.

The C++ workbench never imports torch. It starts `train.py` / `predict.py` as
child processes and talks to them through JSON on stdout, so a crash or an OOM
inside torch cannot take the editor down with it.
"""

__version__ = "0.8.0"
