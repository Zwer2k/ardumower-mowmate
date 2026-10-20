// Hängt ein Element direkt an <body>: Dialoge innerhalb der Dashboards lagen
// sonst im Stapel- und Überlauf-Kontext ihres Containers (von der
// Sensorleiste überdeckt, unten aus dem Bild).
export function portal(node: HTMLElement) {
  if (typeof document === "undefined") return {};
  document.body.appendChild(node);
  return {
    destroy() {
      node.remove();
    },
  };
}
