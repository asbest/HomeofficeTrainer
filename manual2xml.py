import xml.etree.ElementTree as ET

def generate_xml_instructions(keywords, title="Anleitung", step_keyword="Schritt"):
    """
    Erstellt eine XML-strukturierte Anleitung aus einer Liste von Stichworten.

    Args:
        keywords (list): Eine Liste von Stichworten, wobei jeder Eintrag
                         einen Schritt oder eine wichtige Information darstellt.
        title (str): Der Titel der gesamten Anleitung.
        step_keyword (str): Das Präfix für die Bezeichnung jedes Schrittes im XML.

    Returns:
        str: Eine Zeichenkette, die die generierte XML-Anleitung enthält.
    """
    # Erstelle das Wurzel-Element für die Anleitung
    instructions = ET.Element("Anleitung")
    instructions.set("Titel", title)

    # Füge eine Beschreibung hinzu (optional, hier als Beispiel)
    description = ET.SubElement(instructions, "Beschreibung")
    description.text = f"Dies ist eine Anleitung basierend auf den folgenden {len(keywords)} Stichworten."

    # Erstelle das Schritte-Element
    steps_container = ET.SubElement(instructions, "Schritte")

    # Füge jeden Stichwort als separaten Schritt hinzu
    for i, keyword in enumerate(keywords):
        step = ET.SubElement(steps_container, "Schritt")
        step.set("ID", str(i + 1))
        step_title = ET.SubElement(step, "Titel")
        step_title.text = f"{step_keyword} {i + 1}: {keyword}"
        
        # Optional: Füge eine detailliertere Beschreibung zum Schritt hinzu
        # Dies könnte später erweitert werden, wenn die Stichworte komplexer werden
        details = ET.SubElement(step, "Details")
        details.text = f"Führen Sie die Aktion für '{keyword}' aus."

    # Erstelle einen ElementTree-Objekt und wandle es in einen String um
    tree = ET.ElementTree(instructions)
    
    # Optional: Formatieren des XML für bessere Lesbarkeit
    ET.indent(tree, space="  ", level=0) # Verfügbar ab Python 3.9
    
    return ET.tostring(instructions, encoding="utf-8", xml_declaration=True).decode("utf-8")

# --- Beispielanwendung ---
if __name__ == "__main__":
    my_keywords = [
        "1.",
        "2. ",
    ]

    xml_output = generate_xml_instructions(my_keywords, title="Meine Erste Anleitung", step_keyword="Aufgabe")
    print(xml_output)
  
