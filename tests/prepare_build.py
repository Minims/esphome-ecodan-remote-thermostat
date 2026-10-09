"""Copie de compilation avec identifiants factices, sans modifier secrets.yaml."""
import base64
from pathlib import Path
import secrets
import tempfile

root = Path(__file__).resolve().parents[1]
target = Path(tempfile.mkdtemp(prefix="cnrf-build-"))
text = (root / "ecodan-cnrf-proxy-asgard.yaml").read_text()
text = text.replace("path: components", f"path: {root / 'components'}")
(target / "proxy.yaml").write_text(text)
(target / "secrets.yaml").write_text(
    'wifi_ssid: "cnrf-build-test"\nwifi_password: "not-a-real-network"\n'
    f'api_encryption_key: "{base64.b64encode(secrets.token_bytes(32)).decode()}"\n'
    'web_server_username: "test"\n'
    f'web_server_password: "{secrets.token_urlsafe(32)}"\n'
)
print(target / "proxy.yaml")
