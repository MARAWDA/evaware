const boardMap = {
  'esp32-cyd': {
    single: 'HaleHound-CYD-FULL.bin',
    split: 'HaleHound-CYD.bin'
  },
  'esp32-e32r35t': {
    single: 'HaleHound-E32R35T-FULL.bin',
    split: 'HaleHound-E32R35T.bin'
  },
  'esp32-e32r28t': {
    single: 'HaleHound-E32R28T-FULL.bin',
    split: 'HaleHound-E32R28T.bin'
  },
};

const firmwareList = document.getElementById('firmwareList');
const firmwareSelect = document.getElementById('firmwareSelect');
const boardSelect = document.getElementById('boardSelect');
const portSelect = document.getElementById('portSelect');
const flashModeSelect = document.getElementById('flashModeSelect');
const customFirmwareInput = document.getElementById('customFirmware');
const flashBtn = document.getElementById('flashBtn');
const eraseBtn = document.getElementById('eraseBtn');
const refreshBtn = document.getElementById('refreshPorts');
const logOutput = document.getElementById('logOutput');

const setLog = (message) => {
  logOutput.textContent = message;
};

function renderFirmwareTags(fileNames) {
  firmwareList.innerHTML = '';
  fileNames.forEach((fileName) => {
    const badge = document.createElement('span');
    badge.textContent = fileName;
    firmwareList.appendChild(badge);
  });
}

function updateRecommendedFiles() {
  const board = boardSelect.value;
  const mode = flashModeSelect.value;
  const recommended = [boardMap[board][mode] || boardMap[board].single];
  const allFiles = Object.values(boardMap)
    .flatMap((entry) => [entry.single, entry.split])
    .filter((value, index, arr) => arr.indexOf(value) === index)
    .sort();

  firmwareSelect.innerHTML = '';
  const ordered = [...new Set([...recommended, ...allFiles])];

  ordered.forEach((fileName) => {
    const option = document.createElement('option');
    option.value = fileName;
    option.textContent = fileName;
    firmwareSelect.appendChild(option);
  });

  renderFirmwareTags(allFiles);
}

async function loadFiles() {
  try {
    const response = await fetch('/api/files');
    const data = await response.json();

    const fileOptions = data.files || [];
    const board = boardSelect.value;
    const mode = flashModeSelect.value;
    const recommended = [boardMap[board][mode] || boardMap[board].single];
    const allFiles = Object.values(boardMap)
      .flatMap((entry) => [entry.single, entry.split])
      .filter((value, index, arr) => arr.indexOf(value) === index)
      .sort();
    const optionMap = new Set([...recommended, ...allFiles]);

    fileOptions.forEach((entry) => optionMap.add(entry.name));

    firmwareSelect.innerHTML = '';
    [...optionMap].sort().forEach((fileName) => {
      const option = document.createElement('option');
      option.value = fileName;
      option.textContent = fileName;
      firmwareSelect.appendChild(option);
    });

    renderFirmwareTags([...optionMap].sort());

    if (!firmwareSelect.value && firmwareSelect.options.length) {
      firmwareSelect.selectedIndex = 0;
    }
  } catch (error) {
    const fallbackFiles = Object.values(boardMap)
      .flatMap((entry) => [entry.single, entry.split])
      .filter((value, index, arr) => arr.indexOf(value) === index)
      .sort();
    renderFirmwareTags(fallbackFiles);
    setLog('Could not load available firmware files. You can still upload a custom .bin file.');
  }
}

async function refreshPorts() {
  portSelect.innerHTML = '<option value="">Loading ports...</option>';

  try {
    const response = await fetch('/api/ports');
    const data = await response.json();
    const ports = data.ports || [];

    if (!ports.length) {
      portSelect.innerHTML = '<option value="">No serial ports found</option>';
      setLog('No serial ports were detected. Check that the board is connected and the USB driver is installed.');
      return;
    }

    portSelect.innerHTML = '';
    ports.forEach((port) => {
      const option = document.createElement('option');
      option.value = port.name;
      option.textContent = `${port.name} — ${port.description || 'USB serial device'}`;
      portSelect.appendChild(option);
    });

    setLog('Available serial ports loaded. Select the board and click Flash firmware.');
  } catch (error) {
    setLog('Could not read serial ports. The local server may not be running or the USB driver may be missing.');
  }
}

boardSelect.addEventListener('change', () => {
  updateRecommendedFiles();
  setLog(`Board detected: ${boardSelect.options[boardSelect.selectedIndex].text}. Select the port and flash.`);
});

flashModeSelect.addEventListener('change', () => {
  updateRecommendedFiles();
  const modeText = flashModeSelect.value === 'single' ? 'single-file' : 'four-file';
  setLog(`Flash mode switched to ${modeText}. Choose the correct firmware image and continue.`);
});

flashBtn.addEventListener('click', async () => {
  const port = portSelect.value;
  if (!port) {
    setLog('No COM port is selected. Refresh the ports list and choose the correct board connection.');
    return;
  }

  const formData = new FormData();
  formData.append('port', port);
  formData.append('board', boardSelect.value);
  formData.append('flash_mode', flashModeSelect.value);

  if (customFirmwareInput.files && customFirmwareInput.files.length > 0) {
    formData.append('custom_firmware', customFirmwareInput.files[0]);
    setLog(`Uploading custom firmware: ${customFirmwareInput.files[0].name}`);
  } else {
    const selectedName = firmwareSelect.value;
    formData.append('firmware_name', selectedName);
    setLog(`Starting ${flashModeSelect.value === 'single' ? 'single-file' : 'four-file'} flash with: ${selectedName}`);
  }

  flashBtn.disabled = true;
  flashBtn.textContent = 'Flashing...';

  try {
    const response = await fetch('/api/flash', {
      method: 'POST',
      body: formData,
    });

    const data = await response.json();
    setLog(data.output || 'No output was returned by the flashing tool.');

    if (data.ok) {
      setLog('Flashing completed successfully. Power-cycle the board and reconnect it if needed.');
    } else {
      setLog(data.output || 'Flashing failed.');
    }
  } catch (error) {
    setLog('The flash request failed. Make sure the web server is running and the board is connected.');
  } finally {
    flashBtn.disabled = false;
    flashBtn.textContent = 'Flash firmware';
  }
});

refreshBtn.addEventListener('click', refreshPorts);

eraseBtn.addEventListener('click', async () => {
  const port = portSelect.value;
  if (!port) {
    setLog('No COM port is selected. Refresh the ports list and choose the board connection.');
    return;
  }

  if (!window.confirm(`Erase all flash contents on ${port}? This cannot be undone.`)) {
    return;
  }

  const formData = new FormData();
  formData.append('port', port);
  eraseBtn.disabled = true;
  flashBtn.disabled = true;
  eraseBtn.textContent = 'Erasing...';
  setLog(`Erasing all flash contents on ${port}...`);

  try {
    const response = await fetch('/api/erase', {
      method: 'POST',
      body: formData,
    });
    const data = await response.json();
    setLog(data.ok
      ? 'Flash erased successfully. Select a firmware image and flash the board.'
      : (data.output || 'Flash erase failed.'));
  } catch (error) {
    setLog('The erase request failed. Make sure the web server is running and the board is connected.');
  } finally {
    eraseBtn.disabled = false;
    flashBtn.disabled = false;
    eraseBtn.textContent = 'Erase flash';
  }
});

updateRecommendedFiles();
loadFiles();
refreshPorts();
