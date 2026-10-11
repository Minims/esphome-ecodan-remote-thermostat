"""Check proxy mode guards with the actual ESPHome configuration validator."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = f"""
esphome:
  name: cnrf-config-test
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
external_components:
  - source:
      type: local
      path: {ROOT / 'components'}
    components: [ecodan]
uart:
  - id: ftc
    rx_pin: GPIO38
    tx_pin: GPIO39
    baud_rate: 2400
    parity: EVEN
  - id: receiver
    rx_pin: GPIO8
    tx_pin: GPIO7
    baud_rate: 2400
    parity: EVEN
ecodan:
  id: bridge
  uart_id: ftc
  proxy_uart_id: receiver
  proxy_mode: true
  proxy_controller: 1
  proxy_override_timeout: 15min
"""
CASES = [
    ("proxy valide", BASE, True, ""),
    ("délai personnalisé", BASE.replace("15min", "5min"), True, ""),
    ("délai nul", BASE.replace("15min", "0s"), False, "higher than 0s"),
    ("UART identiques", BASE.replace("proxy_uart_id: receiver", "proxy_uart_id: ftc"), False, "distinct"),
    ("débit incorrect", BASE.replace("baud_rate: 2400", "baud_rate: 9600"), False, "2400"),
    ("UART esclave absente", BASE.replace("  proxy_uart_id: receiver\n", ""), False, "proxy_uart_id"),
    ("RC invalide", BASE.replace("proxy_controller: 1", "proxy_controller: 9"), False, "8"),
    ("émulation conservée", BASE.replace("  proxy_mode: true\n  proxy_controller: 1\n", ""), True, ""),
]
with tempfile.TemporaryDirectory(prefix="cnrf-config-") as tmp:
    for name, source, valid, message in CASES:
        path = Path(tmp) / "test.yaml"
        path.write_text(source)
        result = subprocess.run([sys.executable, "-m", "esphome", "config", str(path)], capture_output=True, text=True)
        output = result.stdout + result.stderr
        assert (result.returncode == 0) == valid, f"{name}: {output}"
        assert valid or message in output, f"{name}: {output}"
        print(f"OK: {name}")
