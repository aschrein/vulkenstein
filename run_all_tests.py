import os

for root, dirs, files in os.walk("tests"):
  path = root.split(os.sep)
  for file in files:
    file_path = root + "/" + file
    import subprocess
    cmd = "sh test_shader.sh " + file_path + ""
    print("running: " + cmd)
    process = subprocess.Popen(cmd.split(' '), stdout=subprocess.PIPE)
    stdout, stderr = process.communicate()
    stdout = stdout.decode()
    print(stdout)
    if stdout != None and stdout != '' and not "[SUCCESS]" in stdout:
      print("Couldn't find [SUCCESS]")
      exit(-1)
    if process.returncode != 0:
      exit(-1)
