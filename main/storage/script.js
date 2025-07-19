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
    if (confirm(`Reset và học lệnh cho nút "${commandName}"?`)) {
        // Gửi lệnh học với key là tên nút mặc định
        learnCommandWithKey('single', commandName);
    }
}

// Hàm này bạn cần định nghĩa để gọi lệnh học theo key cụ thể
function learnCommandWithKey(mode, key) {
    console.log(`Học lệnh với key: ${key}, mode: ${mode}`);
    // Gửi yêu cầu học IR lên ESP32 (tùy bạn đã làm như nào)
    fetch(`/ir/learn?mode=${mode}&key=${key}`)
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
    fetch(`/ir/learn?mode=${mode}`)
        .then(res => res.json())
        .then(data => {
            const name = prompt("Nhập tên cho lệnh vừa học:");
            if (name) {
                fetch(`/ir/save?name=${name}`);
                const btn = document.createElement("button");
                btn.textContent = name;
                btn.onclick = () => sendIR(name);
                document.getElementById("customButtons").appendChild(btn);
            }
        });
}

function loadIRList() {
    fetch('/ir/list')
        .then(res => res.json())
        .then(commands => {
            const list = document.getElementById("commandList");
            list.innerHTML = '';
            commands.forEach(name => {
                const li = document.createElement('li');
                li.innerHTML = `
                    <span>${name}</span>
                    <div>
                        <button onclick="sendIR('${name}')">Send</button>
                        <button onclick="renameCommand('${name}')">Rename</button>
                        <button onclick="deleteCommand('${name}')">Delete</button>
                    </div>
                `;
                list.appendChild(li);
            });
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