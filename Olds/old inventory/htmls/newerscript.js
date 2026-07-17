const item = document.getElementById("userItem");

let dragging = false;

item.addEventListener("mousedown", () => dragging = true);
document.addEventListener("mouseup", () => dragging = false);

document.addEventListener("mousemove", e => {
  if (!dragging) return;

  const svg = document.getElementById("workspace");
  const pt = svg.createSVGPoint();
  pt.x = e.clientX;
  pt.y = e.clientY;

  const cursor = pt.matrixTransform(svg.getScreenCTM().inverse());
  item.setAttribute("cx", cursor.x);
  item.setAttribute("cy", cursor.y);
});
