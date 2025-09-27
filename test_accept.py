#!/usr/bin/env python3
import requests

response = requests.post('http://localhost:8080/autollm/accept', data={'job_id': '61'})
print(f"Status: {response.status_code}")
print(f"Headers: {response.headers}")
print(f"Redirected to: {response.headers.get('Location', 'No redirect')}")