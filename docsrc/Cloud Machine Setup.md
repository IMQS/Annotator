## Steps for setting up a GPU road processor VM in GCP
1. Launch a VM with a single GPU (K80 or P4 is probably best cost/performance tradeoff), Ubuntu 18.04 LTS Minimal, 20 GB SSD.
2. Execute this:
	```bash
	# Install NVidia drivers
	sudo apt-get update
	sudo apt-get install -y software-properties-common
	sudo add-apt-repository ppa:graphics-drivers
	sudo apt-get install -y nvidia-headless-410 nvidia-driver-410 nvidia-utils-410
	sudo reboot

	# After reboot, test NVidia drivers
	nvidia-smi

	# Install docker
	curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
	sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
	sudo apt-get install docker-ce docker-ce-cli containerd.io
	```
3. Add nvidia-docker apt repos. See https://nvidia.github.io/nvidia-docker/ for instructions.  
Then, install nvidia-docker-2:
	```bash
	sudo apt-get install nvidia-docker2
	sudo systemctl restart docker
	# Test NVidia docker runtime
	sudo docker run --runtime nvidia hello-world
	```
4. Change the default docker runtime to the NVidia runtime:  
Add this to the top of `/etc/docker/daemon.json`
	```json
	"default-runtime": "nvidia"
	```
5. Test all together
	```bash
	sudo docker run --rm -it nvidia/cuda:10.0-runtime nvidia-smi
	```
6. Login to docker
	```bash
	sudo docker login
	# enter your credentials
	```

You should now be able to run the road processor:
```bash
sudo docker run imqs/roadprocessor --help
# It will take a while on the first run

# Example run
sudo docker run imqs/roadprocessor photos admin@imqs.co.za PASSWORD za.nl.um.-- 2019/2019-02-27/161GOPRO
```
