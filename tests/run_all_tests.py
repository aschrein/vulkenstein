import os, sys

start = 0
end = 0xffffffff

"""
Usage cd {PROJECT_ROOT} && python3 run_all_tests.py [start:i32 [end:i32]]
"""

if len(sys.argv) > 1:
  start = int(sys.argv[1])

if len(sys.argv) > 2:
  end = int(sys.argv[2])

counter = 0

for root, dirs, files in os.walk("."):
  for file in files:
    if not "glsl" in root and not "hlsl" in root:
      continue
    if counter < start:
      counter += 1
      continue
    if counter > end:
      exit(0)
    file_path = root + "/" + file
    import subprocess
    cmd = "sh tests/test_shader.sh " + file_path + ""
    print("running id: " + str(counter) + " cmd: " + cmd)
    process = subprocess.Popen(cmd.split(' '), stdout=subprocess.PIPE)
    stdout, stderr = process.communicate()
    stdout = stdout.decode()
    print(stdout)
    if stdout != None and stdout != '' and not "[SUCCESS]" in stdout:
      print("Couldn't find [SUCCESS]")
      exit(-1)
    if process.returncode != 0:
      exit(-1)
    counter += 1
