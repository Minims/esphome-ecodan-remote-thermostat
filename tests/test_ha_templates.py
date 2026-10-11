"""Exercise the example templates; this does not replace testing in Home Assistant."""
from datetime import datetime, timedelta, timezone
import math
from pathlib import Path
import jinja2
import yaml

package = yaml.safe_load((Path(__file__).resolve().parents[1] / 'home_assistant_package.example.yaml').read_text())
condition = package['automation'][0]['actions'][0]['choose'][0]['conditions'][0]['value_template']
payload = package['script']['cnrf_proxy_temperature_chambre']['sequence'][0]['data']['sample_age_ms']
env = jinja2.Environment()
def is_number(value):
    try:
        return math.isfinite(float(value))
    except (ValueError, TypeError):
        return False

def timestamp(value, default=None):
    if isinstance(value, datetime):
        return value.timestamp()
    try:
        return datetime.fromisoformat(value).timestamp()
    except (ValueError, TypeError):
        return default

env.filters['is_number'] = is_number
night = datetime(2026, 10, 9, 23, 0, tzinfo=timezone.utc)
cases = [
    ('stable fraîche', '16.5', 5, night, False, True),
    ('ancienne valeur numérique', '16.5', 600, night, False, False),
    ('unknown', 'unknown', 5, night, False, False),
    ('unavailable', 'unavailable', 5, night, False, False),
    ('NaN', 'nan', 5, night, False, False),
    ('hors plage', '31', 5, night, False, False),
    ('horodatage absent', '16.5', None, night, False, False),
    ('horodatage futur', '16.5', -1, night, False, False),
    ('cache restauré', '16.5', 5, night, True, False),
    ('retour 7 h', '16.5', 5, night.replace(hour=7), False, False),
    ('début 22 h 30', '16.5', 5, night.replace(hour=22, minute=30), False, True),
]
for name, temp, age, now, restored, expected in cases:
    stamp = (now - timedelta(seconds=age)).isoformat() if age is not None else 'unknown'
    states = {'sensor.temperature_chambre': temp, 'sensor.temperature_chambre_last_seen': stamp,
              'sensor.ecodan_cnrf_proxy_override_timeout': '600'}
    context = dict(now=lambda: now, states=lambda entity: states[entity],
                   state_attr=lambda entity, attr: restored, as_timestamp=timestamp)
    actual = env.from_string(condition).render(**context).strip()
    assert actual == str(expected), (name, actual)
    if expected:
        result = env.from_string(payload).render(**context, sample_timestamp=stamp)
        assert int(result) == age * 1000
for age, timeout, expected in [(700, '900', True), (900, '900', False),
                                (300, '300', False), (5, 'unavailable', False),
                                (5, '0', False), (5, 'nan', False)]:
    states = {'sensor.temperature_chambre': '20',
              'sensor.temperature_chambre_last_seen': (night - timedelta(seconds=age)).isoformat(),
              'sensor.ecodan_cnrf_proxy_override_timeout': timeout}
    context = dict(now=lambda: night, states=lambda entity: states[entity],
                   state_attr=lambda *args: False, as_timestamp=timestamp)
    actual = env.from_string(condition).render(**context).strip()
    assert actual == str(expected), ('délai configurable', age, timeout, actual)
print(f'Home Assistant: {len(cases) + 6} scénarios de templates OK')
