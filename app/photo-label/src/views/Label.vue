<template>
	<div class="outer">
		<div class='control'>
			<div class='title'>{{dim.niceName}}</div>
		</div>
		<!-- <img class='img' :src='currentImgSrc' /> -->
		<div class='canvasContainer'>
			<canvas class='imgCanvas' ref='imgCanvas'>
			</canvas>
			<div class='scrollContainer'>
				<image-scroller class='scroller'></image-scroller>
			</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';
import { Dimension, DimensionSet } from '@/label';
import ImageScroller from '@/components/ImageScroller.vue';

@Component({
	components: {
		ImageScroller,
	},
})
export default class Label extends Vue {
	@Prop(String) dimid!: string;
	allPhotos: string[] = [];
	currentImg: string = '';
	currentImgEl: HTMLImageElement = document.createElement('img');
	dimensions: DimensionSet = new DimensionSet();
	resizeDrawTimer: number = 0;
	//isResizeBusy: boolean = false;

	@Watch('currentImg') onCurrentImgChanged() {
		this.currentImgEl.onload = () => {
			this.draw();
		};
		this.currentImgEl.src = this.currentImgSrc;
	}

	get dim(): Dimension {
		return this.dimensions.fromID(this.dimid);
	}

	get currentImgSrc(): string {
		return '/api/get_image?image=' + encodeURIComponent(this.currentImg);
	}

	draw() {
		let canvas = this.$refs.imgCanvas as HTMLCanvasElement;
		canvas.width = canvas.offsetWidth;
		canvas.height = canvas.offsetHeight;
		let cx = canvas.getContext('2d')!;
		//cx.fillStyle = '#ddd';
		//cx.fillRect(0, 0, canvas.width, canvas.height);
		// Normally I hate screwing with the aspect ratio of an image, but in this case, I'm actually not so sure that it's a bad thing
		cx.drawImage(this.currentImgEl, 0, 0, canvas.width, canvas.height);
		//cx.strokeStyle = '#000';
		//cx.fillStyle = '#0d0';
		//cx.fillRect(0, 0, 50, 50);
		//cx.moveTo(0, 0);
		//cx.lineTo(30, 30);
		//cx.stroke();
	}

	onResize() {
		/*
		// Do this little trick here to make the canvas element resizable.
		// While a resize operation is busy, we make canvas.width and canvas.height small, so that the
		// flex positioning algorithm can do it's thing.
		// Every 50 milliseconds, we resize the canvas to it's native size.
		let canvas = this.$refs.imgCanvas as HTMLCanvasElement;
		if (!this.isResizeBusy) {
			// make a tiny snapshot of the canvas, so that the user doesn't get a white box while resizing
			let aspect = canvas.width / canvas.height;
			canvas.width = 200;
			canvas.height = canvas.width / aspect;
			this.draw();
			this.isResizeBusy = true;
		}
		if (this.resizeDrawTimer !== 0)
			clearTimeout(this.resizeDrawTimer);
		this.resizeDrawTimer = setTimeout(() => {
			this.resizeDrawTimer = 0;
			this.isResizeBusy = false;
			this.draw();
		}, 50);
		*/
		clearTimeout(this.resizeDrawTimer);
		this.resizeDrawTimer = setTimeout(() => {
			this.draw();
		}, 50);
	}

	mounted() {
		fetch('/api/list_images').then((r) => {
			r.json().then((jr) => {
				this.allPhotos = jr as string[];
				if (this.allPhotos.length !== 0) {
					this.currentImg = this.allPhotos[0];
					this.draw();
				}
			});
		});
		DimensionSet.fetch().then((dset: DimensionSet) => {
			this.dimensions = dset;
		});
		window.addEventListener('resize', this.onResize);
	}

	destroyed() {
		window.removeEventListener('resize', this.onResize);
	}
}
</script>

<style lang="scss" scoped>
.outer {
	display: flex;
	flex-direction: row;
	align-items: stretch;
	justify-content: space-between;
	width: 100%;
	height: 100%;
}
.control {
	margin: 0.5em;
	min-width: 20rem;
}
.title {
	font-size: 1.5rem;
	text-align: center;
}
.img {
	// It doesn't matter what our height is here, so long as it's *something*. flex-grow will cause us to expand
	// to fill the available space
	width: 10%;
	object-fit: contain;
	flex: 1 1 auto;
	margin-bottom: 10px;
	//background-color: #d00;
}
/*
.imgCanvas {
	//width: 200px; // This is effectively our minimum width
	//flex: 1 1 auto;
	//position: absolute;
}
*/
.canvasContainer {
	// width: 200px; // This is effectively our minimum width
	flex: 1 1 auto;
	z-index: 1;
	display: flex;
	position: relative;
}
.imgCanvas {
	width: 200px; // This is effectively our minimum width
	flex: 1 1 auto;
}
.scrollContainer {
	position: absolute;
	width: 100%;
	height: 100%;
	//background-color: rgba(255, 0, 0, 0.2);
	display: flex;
	flex-direction: column-reverse;
	align-items: center;
}
.scroller {
	margin: 30px;
}
</style>
