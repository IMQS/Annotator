import json
from pathlib import Path

with open('dims/roads-m3-cou.json') as f:
    jDoc = json.load(f)
jDoc['traffic_signs']['values'] = {}
jSigns = jDoc['traffic_signs']['values']

files = list(Path('app/photo-label/public/traffic-signs').glob("*"))
files.sort()
for imgPath in files:
    if imgPath.suffix != '.jpeg' and imgPath.suffix != '.jpg':
        continue
    parts = imgPath.stem.split(' ')
    code = 'za-' + parts[0]
    title = ' '.join(parts[1:])
    if code in jSigns:
        raise Exception(code + " appears twice")
    jSigns[code] = {
        'title': title,
        'icon': '/traffic-signs/' + imgPath.name,
    }

#print(json.dumps(jDoc, indent=4))

with open('dims/roads-m3-cou.json', 'w') as f:
    json.dump(jDoc, f, indent=4)
