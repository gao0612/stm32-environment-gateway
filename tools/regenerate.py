"""CubeMX先在build内生成，再合并用户区；不覆盖App/库/参数分区。"""
from pathlib import Path
import argparse,subprocess,shutil,re,datetime,json
ROOT=Path(__file__).resolve().parents[1]

def text(path): return path.read_text(encoding='utf-8-sig')
def getblock(s,name):
    match=re.search(r'/\* USER CODE BEGIN '+re.escape(name)+r' \*/(.*?)/\* USER CODE END '+re.escape(name)+r' \*/',s,re.S)
    if not match:raise RuntimeError('Missing user block '+name)
    return match[1].strip()
def block(s,name,content):
    pattern=r'(/\* USER CODE BEGIN '+re.escape(name)+r' \*/).*?(/\* USER CODE END '+re.escape(name)+r' \*/)'
    out,n=re.subn(pattern,lambda m:m[1]+'\n'+content.rstrip()+'\n  '+m[2],s,flags=re.S)
    if n!=1: raise RuntimeError('Missing/duplicate USER CODE block: '+name)
    return out
def preserve(old,new):
    for name,value in re.findall(r'/\* USER CODE BEGIN ([^*]+?) \*/(.*?)/\* USER CODE END \1 \*/',old,re.S):
        if re.search(r'/\* USER CODE BEGIN '+re.escape(name)+r' \*/',new):new=block(new,name,value)
    return new
def merge(generated):
    required=['Core/Src/main.c','Core/Src/freertos.c','Core/Src/spi.c','Core/Src/iwdg.c','Core/Inc/main.h','cmake/stm32cubemx/CMakeLists.txt']
    for f in required:
        if not (generated/f).is_file():raise RuntimeError('CubeMX did not create '+f)
    if 'hspi2' not in text(generated/'Core/Src/spi.c') or 'FLASH_CS_Pin' not in text(generated/'Core/Inc/main.h'):
        raise RuntimeError('SPI2/FLASH_CS missing from generated configuration')
    changes={}
    for f in (generated/'Core').rglob('*'):
        if not f.is_file():continue
        rel=f.relative_to(generated);s=text(f)
        if (ROOT/rel).is_file():s=preserve(text(ROOT/rel),s)
        changes[str(rel)]=s
    main=changes[str(Path('Core/Src/main.c'))]
    main=re.sub(r'^\s*AppWatchdog_CaptureReset\(\);[^\n]*','',main,flags=re.M)
    main=block(main,'1','  AppWatchdog_CaptureReset();\n'+getblock(main,'1'))
    includes=re.search(r'/\* USER CODE BEGIN Includes \*/(.*?)/\* USER CODE END Includes \*/',main,re.S)[1]
    if '#include "gateway.h"' not in includes:includes+='\n#include "gateway.h"\n'
    main=block(main,'Includes',includes)
    # CubeMX would start IWDG too early. The App wrapper starts it only once.
    main=re.sub(r'^\s*MX_IWDG_Init\(\);[^\n]*','',main,flags=re.M)
    main=re.sub(r'^\s*MX_SPI2_Init\(\);[^\n]*','',main,flags=re.M)
    startup=getblock(main,'2')
    if 'Gateway_Init();' not in startup:startup+='\n  Gateway_Init();'
    startup=startup.replace('Gateway_Init();','MX_SPI2_Init();\n  Gateway_Init();')
    if startup.startswith('MX_SPI2_Init();'):startup='  '+startup
    main=block(main,'2',startup)
    changes[str(Path('Core/Src/main.c'))]=main
    freertos=changes[str(Path('Core/Src/freertos.c'))]
    includes=getblock(freertos,'Includes')
    if '#include "gateway.h"' not in includes:includes+='\n#include "gateway.h"'
    freertos=block(freertos,'Includes',includes)
    threads=getblock(freertos,'RTOS_THREADS')
    if 'Gateway_StartTasks();' not in threads:threads+='\n  Gateway_StartTasks();'
    freertos=block(freertos,'RTOS_THREADS',threads)
    freertos=block(freertos,'StartDefaultTask','  (void)argument;\n  osThreadExit(); /* CubeMX占位任务退出，业务任务单独运行。 */')
    freertos=block(freertos,'4',"""void vApplicationStackOverflowHook(xTaskHandle task, signed char *name)
{
    (void)task; (void)name;
    Error_Handler();
}""")
    freertos=block(freertos,'5',"""void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}""")
    changes[str(Path('Core/Src/freertos.c'))]=freertos
    cfg=changes[str(Path('Core/Inc/FreeRTOSConfig.h'))]
    defines=getblock(cfg,'Defines')
    if '#undef INCLUDE_uxTaskGetStackHighWaterMark' not in defines:
        defines+='\n#undef INCLUDE_uxTaskGetStackHighWaterMark\n#define INCLUDE_uxTaskGetStackHighWaterMark 1'
    cfg=block(cfg,'Defines',defines)
    changes[str(Path('Core/Inc/FreeRTOSConfig.h'))]=cfg
    cmake=text(generated/'cmake/stm32cubemx/CMakeLists.txt').replace('\\','/')
    # Older CubeMX for Windows emits absolute and sometimes duplicated source paths.
    lines=[];seen=set()
    for line in cmake.splitlines():
        line=re.sub(r'[A-Za-z]:/[^\n]*?/(Core/[^\s]+)',r'../../\1',line)
        line=re.sub(r'[A-Za-z]:/[^\n]*?/(startup_stm32[^\s]+)',r'../../\1',line)
        if line.strip().endswith(('.c','.s','.S')):
            if line.strip() in seen:continue
            seen.add(line.strip())
        lines.append(line)
    changes[str(Path('cmake/stm32cubemx/CMakeLists.txt'))]='\n'.join(lines)+'\n'
    changes['CMakeLists.txt']=text(ROOT/'cmake/project.cmake')
    toolchain=text(ROOT/'cmake/gcc-arm-none-eabi.cmake')
    toolchain=toolchain.replace('${CMAKE_SOURCE_DIR}/STM32F407ZGTx_FLASH.ld','${CMAKE_SOURCE_DIR}/cmake/gateway_flash.ld')
    changes[str(Path('cmake/gcc-arm-none-eabi.cmake'))]=toolchain
    backup=ROOT/'build/regeneration_backup'/datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    for rel,s in changes.items():
        target=ROOT/rel
        if target.exists():
            old=backup/rel;old.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,old)
        target.parent.mkdir(parents=True,exist_ok=True);target.write_text(s,encoding='utf-8')
    # Preserve sysmem.c/syscalls.c if the CubeMX 6.11 Windows path bug omits them.
    for name in ['sysmem.c','syscalls.c']:
        if not (ROOT/'Core/Src'/name).is_file():raise RuntimeError('Missing '+name)
    print('Merged generated Core. Backup:',backup)
    return changes

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--merge-only',type=Path)
    ap.add_argument('--cubemx',type=Path,default=Path(r'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX'))
    args=ap.parse_args()
    if args.merge_only:merge(args.merge_only.resolve())
    else:
        ioc=ROOT/'gateway.ioc'
        if not ioc.is_file():raise SystemExit('Save your configuration as gateway.ioc first')
        work=ROOT/'build/cubemx'/datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')
        work.mkdir(parents=True)
        shutil.copy2(ioc,work/'gateway.ioc')
        script=work/'generate.txt'
        script.write_text('config load '+(work/'gateway.ioc').as_posix()+'\nproject name gateway\nproject path '+(work/'output').as_posix()+'\nproject toolchain CMake\nproject generate\nexit\n',encoding='utf-8')
        java=args.cubemx/'jre/bin/java.exe';jar=args.cubemx/'STM32CubeMX.exe'
        with (work/'generator.log').open('w',encoding='utf-8') as log:
            subprocess.run([str(java),'-jar',str(jar),'-q',str(script)],stdout=log,stderr=subprocess.STDOUT,check=True)
        merge(work/'output/gateway')
        print('Next: cmake --preset Debug && cmake --build --preset Debug')
