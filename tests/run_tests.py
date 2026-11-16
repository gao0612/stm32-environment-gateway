"""Run with Python 3 and a native GCC on PATH; no STM32 connected."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/host_tests';out.mkdir(parents=True,exist_ok=True)
cases=[('retry',['App/Protocol/retry_delay.c','tests/test_retry.c'],['App/Protocol']),('store',['App/Storage/flash_store.c','tests/test_store.c'],['tests/mock','App/Storage','App/Drivers']),
       ('mqtt',['App/Protocol/mqtt_packet.c','tests/test_mqtt.c'],['App/Protocol'])]
for name,sources,inc in cases:
    exe=out/(name+'.exe')
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-O2',*[str(root/s) for s in sources],
                    *['-I'+str(root/i) for i in inc],'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

exe=out/'network.exe'
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
    '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-O2',
    str(root/'tests/test_network.c'),str(root/'App/Protocol/mqtt_packet.c'),str(root/'App/Protocol/retry_delay.c'),
    *['-I'+str(root/i) for i in ['tests/net_mock','App/Tasks','App/Storage','App/Protocol','App/Config','App/Data']],
    '-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)

subprocess.run([__import__('sys').executable,str(root/'tests/check_startup.py')],check=True)
