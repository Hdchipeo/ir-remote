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
                    cmd.delays.forEach((delay, i) => {
                        const item = document.createElement('li');
                        item.innerHTML = `
                            <div style="display: flex; align-items: center; gap: 8px;">
                                <label>
                                    🕓 Delay ${i + 1} (ms): 
                                    <input type="number" min="0" value="${delay}" id="${cmd.name}-delay-${i}" />
                                </label>
                                <button onclick="deleteDelay('${cmd.name}', ${i})">🗑</button>
                            </div>
                        `;
                        delayList.appendChild(item);
                    });
                } else {
                    const note = document.createElement('p');
                    note.textContent = "⚠️ Không có delay nào!";
                    delayList.appendChild(note);
                }

                const saveBtn = document.createElement('button');
                saveBtn.textContent = "💾 Lưu";
                saveBtn.onclick = () => saveDelays(cmd.name, cmd.delays.length);

                li.appendChild(delayList);
                li.appendChild(saveBtn);
                list.appendChild(li);
            });
        });
}

function saveDelays(key, delayCount) {
    const delays = [];

    for (let i = 0; i < delayCount; i++) {
        const input = document.getElementById(`${key}-delay-${i}`);
        delays.push(input ? parseFloat(input.value) || 0 : 0);
    }

    fetch(`/ir/update_delay?key=${encodeURIComponent(key)}`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: delays.join(',')
    }).then(res => {
        if (res.ok) {
            alert(`Đã cập nhật delay cho "${key}"`);
            loadIRList();
        } else {
            alert("Cập nhật thất bại!");
        }
    });
}

function deleteDelay(name, index) {
    if (!confirm(`Xác nhận xoá Step ${index + 1}?`)) return;

    fetch(`/ir/delete_delay?key=${encodeURIComponent(name)}&index=${index}`, {
        method: 'POST'
    }).then(res => {
        if (res.ok) loadIRList();
        else alert("Không xoá được delay");
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

let availableIRCommands = [];

// Tải danh sách file IR từ server
async function fetchAvailableCommands() {
    try {
        const res = await fetch("/ir/simple_list");
        const commands = await res.json(); // Trả về mảng tên file IR
        availableIRCommands = commands;
        refreshAllSelects();
    } catch (err) {
        alert("❌ Lỗi khi tải danh sách lệnh IR: " + err);
    }
}

// Tải các ánh xạ đã gán sẵn từ server
async function fetchExistingAssignments() {
    try {
        const res = await fetch("/ir/aliases");
        const mappings = await res.json(); // Object: { "white.ir": "reset.ir", ... }

        Object.entries(mappings).forEach(([from, to]) => {
            addAssignPair(from, to);
        });

    } catch (err) {
        console.warn("Không thể tải ánh xạ IR:", err);
    }
}

// Tạo một cặp gán IR mới
function addAssignPair(from = "", to = "") {
    const container = document.getElementById("assign-container");
    const pair = document.createElement("div");
    pair.className = "assign-pair";

    const sourceSelect = document.createElement("select");
    sourceSelect.className = "input-select source-select";

    const arrow = document.createElement("span");
    arrow.textContent = "→";
    arrow.style.margin = "0 10px";

    const targetSelect = document.createElement("select");
    targetSelect.className = "input-select target-select";

    populateSelectOptions(sourceSelect, availableIRCommands);
    populateSelectOptions(targetSelect, availableIRCommands);

    if (from) sourceSelect.value = from;
    if (to) targetSelect.value = to;

    // Nút xoá dòng
    const deleteBtn = document.createElement("button");
    deleteBtn.textContent = "❌";
    deleteBtn.className = "delete-btn";
    deleteBtn.onclick = () => pair.remove();

    pair.appendChild(sourceSelect);
    pair.appendChild(arrow);
    pair.appendChild(targetSelect);
    pair.appendChild(deleteBtn);

    container.appendChild(pair);
}

// Đổ options vào select
function populateSelectOptions(selectElement, options) {
    selectElement.innerHTML = "";
    options.forEach(opt => {
        const option = document.createElement("option");
        option.value = opt;
        option.text = opt;
        selectElement.appendChild(option);
    });
}

// Cập nhật lại toàn bộ <select> khi danh sách lệnh thay đổi
function refreshAllSelects() {
    document.querySelectorAll(".source-select").forEach(sel => populateSelectOptions(sel, availableIRCommands));
    document.querySelectorAll(".target-select").forEach(sel => populateSelectOptions(sel, availableIRCommands));
}

// Gửi toàn bộ các gán lên server
function submitAllAssignments() {
    const pairs = document.querySelectorAll(".assign-pair");
    let assignments = [], errors = [];

    pairs.forEach((pair, i) => {
        const from = pair.querySelector(".source-select").value;
        const to = pair.querySelector(".target-select").value;

        if (from === to) {
            errors.push(`❌ Cặp ${i + 1} không hợp lệ (trùng nhau)`);
        } else {
            assignments.push({ from, to });
        }
    });

    if (errors.length > 0) {
        alert("Lỗi:\n" + errors.join("\n"));
        return;
    }

    fetch("/ir/assign/bulk", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(assignments)
    })
    .then(res => res.text())
    .then(msg => alert("✅ " + msg))
    .catch(err => alert("❌ Gửi thất bại: " + err));
}

// Khi trang load
window.addEventListener("DOMContentLoaded", async () => {
    await fetchAvailableCommands();
    await fetchExistingAssignments();

    if (document.querySelectorAll(".assign-pair").length === 0) {
        addAssignPair(); // Nếu chưa có cặp nào, thêm sẵn 1 dòng
    }
});

// Khởi động
loadIRList();