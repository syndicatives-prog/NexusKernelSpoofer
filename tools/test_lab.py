import subprocess, time, os

GAME_PATH = "C:\\games\\test_game.exe"
MAPPER_PATH = "kdmapper.exe"
DRIVER_PATH = "build/Release/NexusKernelSpoofer.sys"
CLIENT_PATH = "client/spoofer_client.exe"

def load_spoofer():
    subprocess.run([MAPPER_PATH, DRIVER_PATH], check=True)

def configure_spoofing():
    subprocess.run([CLIENT_PATH], check=True)

def start_game():
    return subprocess.Popen([GAME_PATH])

def is_banned():
    # Simplificaci?n: comprobar si el juego genera un archivo de log de baneo
    return os.path.exists("C:\\games\\ban.log")

def main():
    while True:
        load_spoofer()
        time.sleep(5)
        configure_spoofing()
        proc = start_game()
        time.sleep(3600)  # 1 hora
        proc.terminate()
        if is_banned():
            print("Banned! Adjusting parameters...")
            break
        else:
            print("Test passed. Restarting...")

if __name__ == "__main__":
    main()
