from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1])
p=root/'src/ios/ios_main.mm';s=p.read_text()
a=s.index('\t// The same inset is reported for both sides');b=s.index('\n}\n\nconst char *InitIos()',a)
s=s[:a]+'''\t// UIKit supplies orientation-correct safe insets. Reserve both sides so the
\t// UI never lands under the cutout after rotation, regardless of orientation.
\t*pLeft = std::ceil(Insets.left * Scale);
\t*pRight = std::ceil(Insets.right * Scale);
'''+s[b:];p.write_text(s)
p=root/'src/game/client/components/menus_settings.cpp';s=p.read_text();old='\t\tUi()->ClipEnable(&MainView);\n\t\tRenderSettingsPage(Animated);';assert s.count(old)==1
s=s.replace(old,'''#if defined(CONF_PLATFORM_IOS)
\t\t// Legacy settings cards draw borders and shadows outside their content box.
\t\tCUIRect ContentClip = MainView;
\t\tContentClip.x -= 12.0f;
\t\tContentClip.w += 24.0f;
\t\tUi()->ClipEnable(&ContentClip);
#else
\t\tUi()->ClipEnable(&MainView);
#endif
\t\tRenderSettingsPage(Animated);''',1);p.write_text(s)
p=root/'ELY_SOURCE_LOCK.json';d=json.loads(p.read_text());d['ios_safe_edges_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest();p.write_text(json.dumps(d,indent=2)+'\n')
print('Applied orientation-correct safe edges and settings card clipping padding')
