"""Run while ATS is closed. Copies brightness config and bindings without replacing existing new-plugin settings."""
from pathlib import Path
import copy,json,shutil,sys,datetime
root=Path(sys.argv[1])
old=root/'spfPlugins/SPF_ConsoleCommandHotkeys/config/settings.json'
new=root/'spfPlugins/SPF_BrightnessHotkeys/config/settings.json'
framework=root/'spfAssets/config/framework_settings.json'
backup=root/'spfAssets/config'/('brightness-split-backup-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir(parents=True,exist_ok=False)
for p in [old,new,framework]:
 if p.exists():shutil.copy2(p,backup/(p.parent.parent.name+'-'+p.name))
before=json.loads(old.read_text(encoding='utf-8-sig'));legacy=copy.deepcopy(before)
current=json.loads(new.read_text(encoding='utf-8-sig')) if new.exists() else {}
settings=current.setdefault('settings',{})
if 'brightness' in before.get('settings',{}):settings.setdefault('brightness',copy.deepcopy(before['settings']['brightness']))
settings.setdefault('options',copy.deepcopy(before.get('settings',{}).get('options',{'show_notifications':True})))
bindings=current.setdefault('keybinds',{})
moved=[]
for group,actions in legacy.get('keybinds',{}).items():
 for action in list(actions):
  if action in ['brightness_brighter','brightness_dimmer','brightness_reset']:
   target=group.replace('SPF_ConsoleCommandHotkeys','SPF_BrightnessHotkeys',1)
   bindings.setdefault(target,{}).setdefault(action,copy.deepcopy(actions[action]))
   del actions[action];moved.append((group,action,target))
legacy.get('settings',{}).pop('brightness',None)
# Verify every unrelated setting and binding remains byte-value equivalent.
expected=copy.deepcopy(before);expected.get('settings',{}).pop('brightness',None)
for group,action,target in moved:
 del expected['keybinds'][group][action]
 assert bindings[target][action]==before['keybinds'][group][action] or new.exists()
assert legacy==expected
new.parent.mkdir(parents=True,exist_ok=True)
new.write_text(json.dumps(current,indent=4),encoding='utf-8')
old.write_text(json.dumps(legacy,indent=4),encoding='utf-8')
fw=json.loads(framework.read_text(encoding='utf-8-sig'))
fw.setdefault('settings',{}).setdefault('plugin_states',{})['SPF_BrightnessHotkeys']={'enabled':True}
framework.write_text(json.dumps(fw,indent=4),encoding='utf-8')
print('Migrated brightness settings and',len(moved),'bindings; unrelated settings verified unchanged. Backup:',backup)
