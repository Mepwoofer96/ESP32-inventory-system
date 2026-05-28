
function populate() {

    let id = document.getElementById("idnum").value;

    if id == 1 {
    let name = "10k resistor".value ;
    let stock ="2".value ;

        document.getElementById("name").value = "";
        document.getElementById("productStock").value = "";
    }
}


[
  { "id": "1", "name": "10k Resistor", "count": 47, "photo": "/photos/resistor.jpg", "pdf": "/sheets/resistor.pdf" },
  { "id": "2", "name": "ESP32 Dev Board", "count": 5, "photo": "/photos/esp32.jpg", "pdf": "/sheets/esp32.pdf" }
]
