"""手动输入真实结果，写入CSV；不自动判定硬件通过。"""
import argparse,csv,datetime,subprocess,hashlib,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--id',required=True)
p.add_argument('--result',required=True,choices=['PASS','FAIL','NOT_RUN'])
p.add_argument('--observed',required=True);p.add_argument('--evidence',default='')
a=p.parse_args();root=Path(__file__).resolve().parents[1]
git=subprocess.run(['git','rev-parse','--short','HEAD'],cwd=root,capture_output=True,text=True)
commit=git.stdout.strip() if git.returncode==0 else 'uncommitted'
elf=root/'build/Debug/gateway_f407.elf'
sha=hashlib.sha256(elf.read_bytes()).hexdigest() if elf.exists() else 'no-elf'
path=root/'docs/results/hardware.csv';path.parent.mkdir(parents=True,exist_ok=True)
new=not path.exists()
with path.open('a',encoding='utf-8-sig',newline='') as f:
 w=csv.writer(f)
 if new:w.writerow(['time','version','commit','elf_sha256','test_id','result','observed','evidence'])
 w.writerow([datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
             re.search(r'GATEWAY_VERSION\s+"([^"]+)"',(root/'App/Config/version.h').read_text(encoding='utf-8'))[1],commit,sha,a.id,a.result,a.observed,a.evidence])
print(path)
