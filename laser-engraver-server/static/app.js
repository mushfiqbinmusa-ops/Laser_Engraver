const powerSlider = document.getElementById('powerSlider');
const powerValue = document.getElementById('powerValue');
const statusTag = document.getElementById('statusTag');
const transportTag = document.getElementById('transportTag');
const gcodeOutput = document.getElementById('gcodeOutput');
const previewImage = document.getElementById('previewImage');
const homeBtn = document.getElementById('homeBtn');
const uploadForm = document.getElementById('uploadForm');

powerSlider.addEventListener('input', () => {
  powerValue.textContent = `${powerSlider.value}%`;
  fetch(`/api/control/laser/power?level=${powerSlider.value}`, { method: 'POST' }).catch((err) => {
    console.error(err);
  });
});

homeBtn.addEventListener('click', async () => {
  const res = await fetch('/api/control/home', { method: 'POST' });
  const data = await res.json();
  console.log(data);
});

uploadForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const input = document.getElementById('svgUpload');
  const file = input.files[0];
  if (!file) return;

  const formData = new FormData();
  formData.append('svg_file', file);
  const res = await fetch('/api/upload', { method: 'POST', body: formData });
  const data = await res.json();
  if (!data.ok) {
    alert(data.detail || 'Upload failed');
    return;
  }

  previewImage.src = data.preview_url;
  gcodeOutput.textContent = 'No G-code available yet.';
  const statusRes = await fetch(`/api/jobs/${data.job_id}/preview`);
  if (statusRes.ok) {
    previewImage.src = data.preview_url;
  }

  const health = await fetch('/api/health');
  const healthData = await health.json();
  if (healthData.esp_connected) {
    statusTag.textContent = 'ONLINE';
  }
});

async function refreshHealth() {
  try {
    const res = await fetch('/api/health');
    const data = await res.json();
    statusTag.textContent = data.esp_connected ? 'ONLINE' : 'OFFLINE';
    transportTag.textContent = data.transport || 'tcp';
  } catch (error) {
    statusTag.textContent = 'OFFLINE';
  }
}

setInterval(refreshHealth, 3000);
refreshHealth();
