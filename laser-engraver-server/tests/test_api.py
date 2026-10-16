from fastapi.testclient import TestClient

from main import app

client = TestClient(app)


def test_health():
    response = client.get('/api/health')
    assert response.status_code == 200
    payload = response.json()
    assert payload['ok'] is True


def test_upload_svg():
    svg = b'<svg width="10" height="10"><path d="M0 0 L10 0 L10 10" /></svg>'
    response = client.post('/api/upload', files={'svg_file': ('test.svg', svg, 'image/svg+xml')})
    assert response.status_code == 200, response.text
    payload = response.json()
    assert payload['ok'] is True
    assert payload['job_id'] > 0
