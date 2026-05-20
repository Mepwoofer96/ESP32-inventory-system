function addProduct() {
    let name = document.getElementById("productName").value;
    let stock = document.getElementById("productStock").value;
    if (name.trim() !== "" && stock.trim() !== "") {
        let table = document.getElementById("productTable");
        let row = table.insertRow();
        row.innerHTML = `<td>${name}</td><td>${stock}</td><td><button onclick="removeProduct(this)">Remove</button></td>`;
        document.getElementById("productName").value = "";
        document.getElementById("productStock").value = "";
    }
}

function removeProduct(button) {
    let row = button.parentNode.parentNode;
    row.parentNode.removeChild(row);
}
