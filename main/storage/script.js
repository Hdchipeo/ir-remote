function showTab(tabId) {
    // Ẩn tất cả tab
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });

    // Hiện tab được chọn
    const targetTab = document.getElementById(tabId);
    if (targetTab) targetTab.classList.add('active');

    // Cập nhật trạng thái nút tab-bar
    const buttons = document.querySelectorAll('.tab-bar button');
    buttons.forEach(btn => {
        btn.classList.remove('active');
        if (btn.getAttribute('onclick').includes(tabId)) {
            btn.classList.add('active');
        }
    });
}

function learnDefault(commandName) {
    if (confirm(`Xác nhận học lệnh cho nút "${commandName}"?`)) {
        // Gửi lệnh học với key là tên nút mặc định
        if (commandName === 'white' || commandName === 'reset') {
            learnCommandWithKey('step', commandName);
        }
        else
            learnCommandWithKey('normal', commandName);
    }
}

// Hàm này bạn cần định nghĩa để gọi lệnh học theo key cụ thể
function learnCommandWithKey(mode, key) {
    console.log(`Học lệnh với key: ${key}, mode: ${mode}`);
    // Gửi yêu cầu học IR lên ESP32 (tùy bạn đã làm như nào)
    fetch(`/ir/learn?mode=${mode}&name=${key}`)
        .then(res => res.text())
        .then(data => alert(`Đã gửi yêu cầu học lệnh: ${key}`))
        .catch(err => alert(`Lỗi: ${err}`));
}

function sendIR(command) {
    fetch(`/ir/send?name=${command}`);
}

function whiteScreen() {
    fetch('/ir/white-screen');
}

function resetScreen() {
    fetch('/ir/reset-screen');
}

function learnCommand(mode) {
    const name = prompt("Nhập tên cho lệnh:");
    if (!name) return;

    fetch(`/ir/learn?mode=${mode}&name=${encodeURIComponent(name)}`)
        .then(res => res.json())
        .then(data => {
            if (data.status === "ok") {
                const btn = document.createElement("button");
                btn.textContent = name;
                btn.onclick = () => sendIR(name);
                document.getElementById("customButtons").appendChild(btn);
            } else {
                alert("Học lệnh thất bại!");
            }
        });
}

function loadIRList() {
    fetch('/ir/list')
        .then(res => res.json())
        .then(commands => {
            const list = document.getElementById("commandList");
            list.innerHTML = '';

            commands.forEach(cmd => {
                const li = document.createElement('li');
                li.innerHTML = `<h3>${cmd.name}</h3>`;

                const delayList = document.createElement('ul');
                delayList.style.listStyle = 'none';
                delayList.style.padding = '0';

                if (cmd.delays.length > 0) {
                    for (let i = 0; i < cmd.delays.length; i++) {
                        const item = document.createElement('li');
                        item.innerHTML = `
                            <label>
                                Delay giữa Step ${i + 1} → Step ${i + 2} (ms): 
                                <input type="number" min="0" value="${cmd.delays[i]}" id="${cmd.name}-delay-${i}" />
                            </label>
                        `;
                        delayList.appendChild(item);
                    }
                } else {
                    const note = document.createElement('p');
                    note.textContent = "⚠️ Không có delay nào được lưu!";
                    delayList.appendChild(note);
                }

                const saveBtn = document.createElement('button');
                saveBtn.textContent = "Save Delay";
                saveBtn.onclick = () => saveDelays(cmd.name, cmd.delays.length);

                li.appendChild(delayList);
                li.appendChild(saveBtn);

                list.appendChild(li);
            });
        })
        .catch(err => {
            console.error("Failed to load IR list", err);
            alert("Failed to load IR command list");
        });
}

function saveDelays(key, delayCount) {
    const delays = [];

    for (let i = 0; i < delayCount; i++) {
        const input = document.getElementById(`${key}-delay-${i}`);
        if (!input) {
            console.warn(`Missing input for ${key}-delay-${i}`);
            delays.push(0);
            continue;
        }

        const val = parseFloat(input.value);
        delays.push(isNaN(val) ? 0 : val);
    }

    fetch(`/ir/update_delay?key=${encodeURIComponent(key)}`, {
        method: 'POST',
        headers: {
            'Content-Type': 'text/plain'
        },
        body: delays.join(',')  // Ex: "1500,800,300,100"
    })
    .then(res => {
        if (res.ok) {
            alert(`Delay cho "${key}" đã được cập nhật!`);
        } else {
            alert(`Không thể cập nhật delay cho "${key}"`);
        }
    })
    .catch(err => {
        console.error("Failed to update delay", err);
        alert("Lỗi kết nối khi cập nhật delay");
    });
}

function renameCommand(name) {
    const newName = prompt("Tên mới:", name);
    if (newName) {
        fetch(`/ir/rename?old=${name}&new=${newName}`);
        loadIRList();
    }
}

function deleteCommand(name) {
    fetch(`/ir/delete?name=${name}`);
    loadIRList();
}

function updateFirmware() {
    fetch('/fw/check')
        .then(res => res.json())
        .then(data => {
            if (data.update) {
                fetch('/fw/update');
                document.getElementById("fwStatus").textContent = "Đang cập nhật...";
            } else {
                document.getElementById("fwStatus").textContent = "Không có bản cập nhật.";
            }
        });
}

// Khởi động
loadIRList();