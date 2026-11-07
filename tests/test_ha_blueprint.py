"""Exercise the shipped blueprint templates without a running Home Assistant."""
from datetime import datetime, timedelta, timezone
import math
from pathlib import Path
from types import SimpleNamespace

import jinja2
import yaml


inputs = {
    'temperature_sensor': 'sensor.bedroom',
    'timeout_sensor': 'number.proxy_timeout',
    'always_active': False,
    'start_time': '22:30:00',
    'end_time': '07:00:00',
    'override_action': 'esphome.custom_cnrf_override',
    'disable_action': 'esphome.custom_cnrf_disable',
}


class BlueprintLoader(yaml.SafeLoader):
    pass


BlueprintLoader.add_constructor('!input', lambda loader, node: inputs[loader.construct_scalar(node)])
blueprint = yaml.load((Path(__file__).resolve().parents[1] /
                       'blueprints/automation/ecodan/cnrf_proxy.yaml').read_text(), Loader=BlueprintLoader)
choice = blueprint['actions'][1]['choose'][0]
condition = choice['conditions'][0]['value_template']
payload = choice['sequence'][0]['data']
env = jinja2.Environment()


class SensorStates(dict):
    def __call__(self, entity):
        sensor = self.get(entity)
        return sensor.state if sensor is not None else 'unknown'

    def __getitem__(self, entity):
        return self.get(entity)


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
night = datetime(2026, 11, 3, 23, tzinfo=timezone.utc)
# Freshness boundaries, invalid data, both schedule shapes and equal hours.
cases = [
    {'name': 'valeur stable avec attribut mis à jour'},
    {'name': 'expiration', 'age': 600, 'expected': False},
    {'name': 'rapport identique sans mise à jour', 'age': 700, 'expected': False},
    {'name': 'arrondi conservateur', 'age': 1.0001},
    {'name': 'unknown', 'temperature': 'unknown', 'expected': False},
    {'name': 'unavailable', 'temperature': 'unavailable', 'expected': False},
    {'name': 'NaN', 'temperature': 'nan', 'expected': False},
    {'name': 'infini', 'temperature': 'inf', 'expected': False},
    {'name': 'borne basse', 'temperature': '10'},
    {'name': 'borne haute', 'temperature': '30'},
    {'name': 'sous la plage', 'temperature': '9.9', 'expected': False},
    {'name': 'hors plage', 'temperature': '30.1', 'expected': False},
    {'name': 'sonde absente', 'missing': True, 'expected': False},
    {'name': 'horodatage futur', 'age': -1, 'expected': False},
    {'name': 'température restaurée', 'restored': 'sensor.bedroom', 'expected': False},
    {'name': 'délai restauré', 'restored': 'number.proxy_timeout', 'expected': False},
    {'name': 'retour 7 h', 'now': night.replace(hour=7), 'expected': False},
    {'name': 'début 22 h 30', 'now': night.replace(hour=22, minute=30)},
    {'name': 'avant début', 'now': night.replace(hour=22, minute=29, second=59), 'expected': False},
    {'name': 'après minuit', 'now': night.replace(hour=0)},
    {'name': 'avant fin', 'now': night.replace(hour=6, minute=59, second=59)},
    {'name': 'délai 15 minutes', 'age': 700, 'timeout': '900'},
    {'name': 'délai 5 minutes expiré', 'age': 300, 'timeout': '300', 'expected': False},
    {'name': 'délai absent', 'timeout': 'unavailable', 'expected': False},
    {'name': 'délai nul', 'timeout': '0', 'expected': False},
    {'name': 'délai négatif', 'timeout': '-1', 'expected': False},
    {'name': 'délai NaN', 'timeout': 'nan', 'expected': False},
    {'name': 'journée début', 'start': '08:00:00', 'end': '18:00:00', 'now': night.replace(hour=8)},
    {'name': 'journée fin', 'start': '08:00:00', 'end': '18:00:00',
     'now': night.replace(hour=18), 'expected': False},
    {'name': 'journée nuit', 'start': '08:00:00', 'end': '18:00:00', 'expected': False},
    {'name': 'permanent en journée', 'always_active': True, 'now': night.replace(hour=12)},
    {'name': 'permanent à 23 h 59', 'always_active': True, 'start': '00:00:00', 'end': '23:59:00',
     'now': night.replace(hour=23, minute=59)},
    {'name': 'permanent heures identiques', 'always_active': True, 'start': '07:00:00', 'end': '07:00:00'},
    {'name': 'permanent mesure expirée', 'always_active': True, 'age': 900, 'timeout': '900', 'expected': False},
    {'name': 'permanent sonde indisponible', 'always_active': True, 'temperature': 'unavailable', 'expected': False},
    {'name': 'permanent sonde restaurée', 'always_active': True, 'restored': 'sensor.bedroom', 'expected': False},
    {'name': 'source fraîche et template ancien', 'age': 700, 'sample_age': 5},
    {'name': 'source périmée et template récent', 'age': 5, 'sample_age': 700, 'expected': False},
    {'name': 'date source future', 'sample_age': -1, 'expected': False},
    {'name': 'date source vide', 'sample_timestamp': '', 'expected': False},
    {'name': 'date source nulle', 'sample_timestamp': None, 'expected': False},
    {'name': 'date source invalide', 'sample_timestamp': 'invalid', 'expected': False},
    {'name': 'heures identiques', 'start': '07:00:00', 'end': '07:00:00', 'expected': False},
]
for case in cases:
    now = case.get('now', night)
    age = case.get('age', 5)
    stamp = (now - timedelta(seconds=age)).isoformat()
    states = {'sensor.bedroom': case.get('temperature', '16.5'),
              'number.proxy_timeout': case.get('timeout', '600')}
    sensor_states = SensorStates({entity: SimpleNamespace(state=value) for entity, value in states.items()})
    sensor_states['sensor.bedroom'].last_updated = datetime.fromisoformat(stamp)
    sensor_states['sensor.bedroom'].attributes = {}
    expected_stamp = stamp
    if 'sample_age' in case:
        expected_stamp = (now - timedelta(seconds=case['sample_age'])).isoformat()
        sensor_states['sensor.bedroom'].attributes['sample_timestamp'] = expected_stamp
    if 'sample_timestamp' in case:
        sensor_states['sensor.bedroom'].attributes['sample_timestamp'] = case['sample_timestamp']
    sensor_states['sensor.bedroom'].last_changed = now - timedelta(days=1)
    sensor_states['sensor.bedroom'].last_reported = now
    if case.get('missing'):
        del sensor_states['sensor.bedroom']
    context = dict(inputs, always_active=case.get('always_active', False), now=lambda: now, states=sensor_states,
                   state_attr=lambda entity, attr: entity == case.get('restored'), as_timestamp=timestamp,
                   start_time=case.get('start', inputs['start_time']),
                   end_time=case.get('end', inputs['end_time']))
    for key, template in blueprint['actions'][0]['variables'].items():
        context[key] = env.from_string(template).render(**context)
    actual = env.from_string(condition).render(**context).strip()
    assert actual == str(case.get('expected', True)), (case['name'], actual)
    if case.get('expected', True):
        assert float(env.from_string(payload['temperature']).render(**context)) == float(states['sensor.bedroom'])
        milliseconds = int(env.from_string(payload['sample_age_ms']).render(**context))
        assert milliseconds == math.ceil((now.timestamp() - timestamp(expected_stamp)) * 1000), case['name']

assert choice['sequence'][0]['action'] == inputs['override_action']
assert blueprint['actions'][1]['default'][0]['action'] == inputs['disable_action']
assert blueprint['triggers'][-1]['entity_id'] == [inputs[key] for key in
                                                ('temperature_sensor', 'timeout_sensor')]
assert 'timestamp_sensor' not in blueprint['blueprint']['input']
assert blueprint['blueprint']['input']['timeout_sensor']['selector']['entity']['filter']['domain'] == 'number'
assert [trigger['at'] for trigger in blueprint['triggers'] if trigger['trigger'] == 'time'] == [
    inputs['start_time'], inputs['end_time']]
print(f'Blueprint CNRF : {len(cases)} scénarios de templates OK')
