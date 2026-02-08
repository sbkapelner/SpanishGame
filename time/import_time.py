import csv
import sqlite3
import os

# Connect to the database
db_path = '../data/spanish_game.db'
conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Dictionary to map filenames to pattern IDs
pattern_map = {
    'Es____.csv': 1,                    # Es ____
    'Es_la_____y____.csv': 2,          # Es la ____ y ____
    'Faltan_____para_la____.csv': 3,    # Faltan ____ para la ____
    'Faltan_____para_las____.csv': 4,   # Faltan ____ para las ____
    'Son_las____.csv': 5,              # Son las ____
    'Son_las_____menos____.csv': 6,    # Son las ____ menos ____
    'Son_las_____y____.csv': 7         # Son las ____ y ____
}

# Process each CSV file
for filename in os.listdir('.'):
    if filename.endswith('.csv') and filename in pattern_map:
        pattern_id = pattern_map[filename]
        print(f"Processing {filename}...")
        
        with open(filename, 'r', encoding='utf-8') as csvfile:
            reader = csv.reader(csvfile)
            next(reader)  # Skip header row
            
            for row in reader:
                if not row:  # Skip empty rows
                    continue
                    
                if len(row) == 1:
                    # Files like Es____.csv and Son_las____.csv have only hour
                    hour = row[0]
                    cursor.execute(
                        "INSERT INTO time_expressions (pattern_id, hour) VALUES (?, ?)",
                        (pattern_id, hour)
                    )
                elif len(row) == 2:
                    # Files like Son_las_____y____.csv have hour and minute
                    hour, minute = row
                    cursor.execute(
                        "INSERT INTO time_expressions (pattern_id, hour, minute) VALUES (?, ?, ?)",
                        (pattern_id, hour, minute)
                    )

# Commit changes and close connection
conn.commit()
conn.close()
print("Import complete!")
