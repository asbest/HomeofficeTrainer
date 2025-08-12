import re
import xml.etree.ElementTree as ET
from datetime import datetime

def parse_unstructured_text(text):
    """
    Zerlegt einen unstrukturierten Text in eine Liste von Schritten.
    Erkennt Zahlen, Bindestriche und Aufzählungspunkte als Trennzeichen.
    """
    candidates = re.split(r'(?:\n|^\d+\.\s*|- |\* |\• )', text)
    steps = [c.strip() for c in candidates if c and c.strip()]
    return steps

def detect_dependencies(steps):
    """
    Erkennt Abhängigkeiten zwischen Schritten basierend auf dem Textinhalt.
    Gibt ein Dict {SchrittID: [Abhängigkeits-IDs]} zurück.
    """
    dependencies = {}
    for i, text in enumerate(steps, start=1):
        deps_for_step = set()

        # Suche nach direktem "Schritt X" Bezug
        matches = re.findall(r"[Ss]chritt\s*(\d+)", text)
        for m in matches:
            num = int(m)
            if 1 <= num < i:  # keine Abhängigkeit zu späteren Schritten
                deps_for_step.add(num)

        # Heuristik: Wörter wie "nachdem", "erst wenn", "sobald"
        if re.search(r"\bnachdem\b|\berst wenn\b|\bsobald\b", text, re.IGNORECASE):
            # einfache Annahme: hängt vom vorherigen Schritt ab
            if i > 1:
                deps_for_step.add(i - 1)

        if deps_for_step:
            dependencies[i] = sorted(deps_for_step)

    return dependencies

def generate_xml_instructions_from_text(raw_text, title="Anleitung", step_keyword="Schritt", dependencies=None):
    """
    Erzeugt eine XML-Struktur aus unstrukturiertem Text mit optionalen oder automatisch erkannten Abhängigkeiten.
    """
    keywords = parse_unstructured_text(raw_text)

    # Falls keine manuelle Dependency-Liste übergeben wurde → automatisch ermitteln
    if dependencies is None:
        dependencies = detect_dependencies(keywords)

    instructions = ET.Element("Anleitung")
    instructions.set("Titel", title)
    instructions.set("ErstelltAm", datetime.now().isoformat())

    description = ET.SubElement(instructions, "Beschreibung")
    description.text = f"Automatisch generiert aus unstrukturiertem Text. {len(keywords)} Schritte erkannt."

    steps_container = ET.SubElement(instructions, "Schritte")

    for i, keyword in enumerate(keywords, start=1):
        step = ET.SubElement(steps_container, "Schritt", ID=str(i))
        
        step_title = ET.SubElement(step, "Titel")
        step_title.text = f"{step_keyword} {i}"
        
        details = ET.SubElement(step, "Details")
        details.text = re.sub(r'^\d+\.\s*', '', keyword)  # Entfernt "1. " etc.

        abhaengigkeiten_elem = ET.SubElement(step, "Abhaengigkeiten")
        for dep_id in dependencies.get(i, []):
            ET.SubElement(abhaengigkeiten_elem, "SchrittRef").text = str(dep_id)

    tree = ET.ElementTree(instructions)
    ET.indent(tree, space="  ", level=0)
    return ET.tostring(instructions, encoding="utf-8", xml_declaration=True).decode("utf-8")


# --- Beispiel mit automatischer Erkennung ---
if __name__ == "__main__":
    raw_text = """
    1. Backofen auf 180 Grad vorheizen
    - Mehl und Zucker vermischen
    • Eier hinzufügen nachdem Mehl und Zucker vermischt wurden
    Butter schmelzen und unterrühren (erst wenn Eier hinzugefügt wurden)
    """
    xml_output = generate_xml_instructions_from_text(
        raw_text,
        title="Kuchenrezept",
        step_keyword="Aufgabe"
    )
    print(xml_output)
