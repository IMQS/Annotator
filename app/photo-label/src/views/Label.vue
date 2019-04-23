<template>
	<div class="outer">
		<div class='control'>
			<div class='controlTop'>
				<div class='titleBar'>
					<router-link to='/' class='backBtn'>⬅</router-link>
					<div class='title'>{{dim ? dim.niceName : 'none'}}</div>
				</div>
				<div style='display: flex; margin-bottom: 0.5em'>
					<div class='skLeft'>⌨️</div>
					<div class='skRight'>value</div>
				</div>
				<div v-if='dim'>
					<div v-for='val in dim.values' :key='val' class='labelRow' :class='{activeLabelRow: activeLabel === val}' @click='onLabelRowClick(val)'>
						<div class='skLeft'>{{dim.valueToShortcutKey[val]}}</div>
						<div class='skRight'>{{val}}</div>
					</div>
					<div class='labelRow'><div class='skLeft'>space</div><div class='skRight' style='color: #a55'>remove label</div></div>
				</div>
				<div class='optionsGroup'>
					<label><input type='checkbox' v-model='drawTextOnLabels' />Show Labels</label>
				</div>
				<div style='margin: 1rem 0.5rem; font-size: 0.85rem; color: #777;'>
					Press the shortcut key in the left column to label the image.
					<br><br>
					Press left/right keys to scan through the images.
					<br><br>
					Hold down <em>CTRL</em> and press left/right to apply the same label to the previous/next image.
				</div>
				<!--
				<div style='display: flex'><div class='skLeft'>←</div><div class='skRight'>previous</div></div>
				<div style='display: flex'><div class='skLeft'>→</div><div class='skRight'>next</div></div>
				-->
			</div>
			<div class='controlBottom'>
				<div style='margin-bottom: 10px'>Dataset</div>
				<dataset-picker @change='onDatasetChanged' style='max-height: 100%; overflow: auto; margin-left: 5px' />
			</div>
		</div>
		<div class='canvasContainer'>
			<canvas class='imgCanvas' ref='imgCanvas' :style='canvasStyle'>
			</canvas>
			<div class='overlayContainer'>
				<div class='overlayInfo'>
					<div>{{currentImgName}}</div>
					<div style='display:flex; align-items: center'>
						<div style='margin-right: 0.3em'>🌞</div>
						<image-scroller style='width: 10rem' :pos='brightness' @change='onBrightnessChange'></image-scroller>
					</div>
				</div>
				<div class='labelTxt' :style='{"font-size": labelTxtFontSize}'>
					{{wholeImageLabel}}
				</div>
				<div style='pointer-events: auto; position: absolute; right: 0px; top: 0px; padding: 10px'>
					<svg-button icon='/maximize-2.svg' @click='draw.zoomAll()'></svg-button>
				</div>
			</div>
			<div class='scrollContainer'>
				<image-scroller class='scroller' :pos='scrollPos' @change='onScroll'></image-scroller>
			</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';
import { Dimension, DimensionSet, DimensionType, DirtyRegion, DirtyRegionQueue, LabelRegion } from '@/label';
import ImageScroller from '@/components/ImageScroller.vue';
import DatasetPicker from '@/components/DatasetPicker.vue';
import SvgButton from '@/components/SvgButton.vue';
import * as draw from '@/draw';
import { ImageLabelSet } from '@/label';

@Component({
	components: {
		ImageScroller,
		DatasetPicker,
		SvgButton,
	},
})
export default class Label extends Vue {
	@Prop(String) dimid!: string; // Dimension that we are labelling

	allPhotos: string[] = [];
	selectedPhotos: string[] = []; // a subset of the photos in allPhotos, which match the prefix 'dataset'
	dataset: string = '';
	dimensions: DimensionSet = new DimensionSet();
	resizeDrawTimer: number = 0;
	imgIndex: number = -1; // index in selectedPhotos
	brightness: number = 0.5;
	ctrlKeyDown: boolean = false;
	waitingForImg: boolean = false;
	dirtyQueue: DirtyRegionQueue = new DirtyRegionQueue();
	draw: draw.Draw = new draw.Draw();
	//isResizeBusy: boolean = false;

	@Watch('imgIndex') onCurrentImgChanged() {
		// Don't load the label for the whole image if control is being held down, because in this case, we are assigning
		// a new label to the whole image, so there's no need to load the previous state.
		if (!this.ctrlKeyDown)
			this.loadLabels();

		this.waitingForImg = true;
		this.draw.img.onload = () => {
			this.waitingForImg = false;
			this.drawCanvas();
		};
		this.draw.img.src = this.currentImgSrc;
	}

	get labels(): ImageLabelSet {
		return this.draw.labels;
	}

	set labels(labels: ImageLabelSet) {
		this.draw.labels = labels;
	}

	get activeLabel(): string {
		return this.draw.activeLabel;
	}

	set activeLabel(label: string) {
		this.draw.activeLabel = label;
	}

	get dim(): Dimension | null {
		return this.dimensions.fromID(this.dimid);
	}

	get isWholeImageDim(): boolean {
		if (this.dim === null)
			return true;
		return this.dim.type === DimensionType.WholeImage;
	}

	get scrollPos(): number {
		if (this.selectedPhotos.length === 0)
			return 0;
		return this.imgIndex / (this.selectedPhotos.length - 1);
	}

	get labelTxtFontSize(): string {
		if (this.wholeImageLabel.length <= 1)
			return '15rem';
		else
			return '7rem';
	}

	get currentImgName(): string {
		if (this.imgIndex >= this.selectedPhotos.length)
			return '';
		return this.selectedPhotos[this.imgIndex];
	}

	get currentImgSrc(): string {
		if (this.imgIndex >= this.selectedPhotos.length)
			return '';
		return '/api/get_image?image=' + encodeURIComponent(this.imgPath);
	}

	get canvasStyle(): object {
		return {
			filter: `brightness(${this.brightness * 200}%)`,
		};
	}

	get imgPath(): string {
		if (this.imgIndex >= 0 && this.imgIndex < this.selectedPhotos.length)
			return this.selectedPhotos[this.imgIndex];
		else
			return '';
	}

	get wholeImageLabel(): string {
		if (this.dim === null || this.dim.type !== DimensionType.WholeImage)
			return '';

		let lab = this.labels.wholeImageRegion.labels[this.dimid];
		if (lab === undefined)
			return '';
		return lab;
	}

	get drawTextOnLabels(): boolean {
		return this.draw.drawText;
	}

	set drawTextOnLabels(enable: boolean) {
		this.draw.drawText = enable;
		localStorage.setItem('showLabels', enable ? '1' : '0');
		this.draw.paint();
	}

	onKeyDown(ev: KeyboardEvent) {
		if (ev.key === 'Control')
			this.ctrlKeyDown = true;

		let seek = 0;
		if (ev.key === 'ArrowLeft')
			seek = -1;
		else if (ev.key === 'ArrowRight')
			seek = 1;

		if (seek !== 0) {
			// If we don't do this, then focus can land up on the dataset radio buttons, and the left/right
			// keys end up switching datasets.
			ev.preventDefault();
		}

		// Only seek if we're not busy waiting for an image. If we don't do this, then we end up
		// cancelling the previous image load, and this happens every time, so we end up never
		// showing any updates. All things considered, it's probably a good thing to make sure
		// that every single frame flashes before the user's eyes when he is labelling.
		if (seek !== 0 && !this.waitingForImg) {
			this.imgIndex = Math.max(0, Math.min(this.imgIndex + seek, this.selectedPhotos.length - 1));
			this.saveScrollPosToLocalStorage();
			if (ev.ctrlKey && this.isWholeImageDim)
				this.setWholeImageLabel(this.activeLabel);
		} else if (this.dim) {
			//console.log(ev);
			let val = this.dim.shortcutKeyToValue[ev.key.toUpperCase()];
			if (this.isWholeImageDim) {
				if (val !== undefined)
					this.setWholeImageLabel(val);
				else if (ev.key === ' ')
					this.setWholeImageLabel('');
			} else {
				if (val !== undefined)
					this.activeLabel = val;
			}
		}
	}

	onKeyUp(ev: KeyboardEvent) {
		if (ev.key === 'Control')
			this.ctrlKeyDown = false;
	}

	onLabelRowClick(dimid: string) {
		this.activeLabel = dimid;
	}

	onBrightnessChange(pos: number) {
		this.brightness = pos;
		localStorage.setItem('brightness', this.brightness.toPrecision(3));
	}

	onScroll(pos: number) {
		let i = pos * this.selectedPhotos.length;
		this.imgIndex = Math.floor(Math.max(0, Math.min(i, this.selectedPhotos.length - 1)));
		this.saveScrollPosToLocalStorage();
	}

	onDatasetChanged(dataset: string) {
		this.dataset = dataset;
		this.refreshSelectedPhotos();
	}

	saveScrollPosToLocalStorage() {
		if (this.imgIndex < this.selectedPhotos.length)
			localStorage.setItem('imgName', this.selectedPhotos[this.imgIndex]);
	}

	restoreImgIndexFromLocalStorage() {
		let old = localStorage.getItem('imgName');
		if (old === null) {
			this.imgIndex = 0;
			return;
		}
		let list = this.selectedPhotos;
		for (let i = 0; i < list.length; i++) {
			if (list[i] === old) {
				this.imgIndex = i;
				return;
			}
		}
		this.imgIndex = 0;
	}

	// Refresh the list of photos available from the scrollbar.
	// This is called after the user changes the dataset.
	refreshSelectedPhotos() {
		if (this.dataset === '' || this.dataset === 'Everything') {
			this.selectedPhotos = this.allPhotos;
			this.restoreImgIndexFromLocalStorage();
			this.onCurrentImgChanged(); // force a change, because imgIndex doesn't mean what it used to
			return;
		}
		let s = [];
		for (let p of this.allPhotos) {
			if (p.startsWith(this.dataset))
				s.push(p);
		}
		this.selectedPhotos = s;
		this.restoreImgIndexFromLocalStorage();
		this.onCurrentImgChanged(); // force a change, because imgIndex doesn't mean what it used to
	}

	setWholeImageLabel(val: string) {
		this.activeLabel = val;
		if (this.dim === null)
			return;
		this.$set(this.labels.wholeImageRegion.labels, this.dimid, val);
		this.dirtyQueue.push(new DirtyRegion(this.imgPath, this.labels.wholeImageRegion, this.dimid));
	}

	drawCanvas() {
		this.draw.paint();
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
			this.drawCanvas();
			this.isResizeBusy = true;
		}
		if (this.resizeDrawTimer !== 0)
			clearTimeout(this.resizeDrawTimer);
		this.resizeDrawTimer = setTimeout(() => {
			this.resizeDrawTimer = 0;
			this.isResizeBusy = false;
			this.drawCanvas();
		}, 50);
		*/
		clearTimeout(this.resizeDrawTimer);
		this.resizeDrawTimer = setTimeout(() => {
			this.drawCanvas();
		}, 50);
	}

	loadLabels() {
		let $this = this;
		let apiURL = '/api/db/get_labels?image=' + encodeURIComponent(this.imgPath);
		fetch(apiURL, { method: 'POST' }).then((response) => {
			if (!response.ok) {
				alert(response.status + ' ' + response.statusText);
			} else {
				response.json().then((jvals) => {
					$this.labels = ImageLabelSet.fromJSON(jvals);
					// If we're waiting for an image, then drawing now will cause us to flash a white screen
					if (!this.waitingForImg)
						$this.drawCanvas();
				});
			}
		}).catch((reason) => {
			alert(reason);
		});
	}

	onRegionModified(action: draw.Modification, region: LabelRegion) {
		if (action === draw.Modification.Modify) {
			if (this.draw.curRegion!.labels[this.dimid] === undefined) {
				// This path only gets hit when a new region is created
				this.$set(this.draw.curRegion!.labels, this.dimid, this.activeLabel);
			}
			this.dirtyQueue.push(new DirtyRegion(this.imgPath, region, this.dimid));
		} else if (action === draw.Modification.Delete) {
			this.dirtyQueue.push(new DirtyRegion(this.imgPath, region, this.dimid));
		}
	}

	mounted() {
		this.draw.onModifyRegion = this.onRegionModified;
		this.draw.initialize(this.$refs.imgCanvas as HTMLCanvasElement);

		let sb = localStorage.getItem('brightness');
		if (sb !== null)
			this.brightness = parseFloat(sb);

		let showLabels = localStorage.getItem('showLabels');
		if (showLabels !== null)
			this.draw.drawText = showLabels === '1';

		fetch('/api/list_images').then((r) => {
			r.json().then((jr) => {
				this.allPhotos = jr as string[];
				this.refreshSelectedPhotos();
				if (this.selectedPhotos.length !== 0) {
					this.restoreImgIndexFromLocalStorage();
					this.drawCanvas();
				}
			});
		});
		DimensionSet.fetch().then((dset: DimensionSet) => {
			this.dimensions = dset;
			this.draw.dim = this.dim;
		});
		window.addEventListener('resize', this.onResize);
		window.addEventListener('keydown', this.onKeyDown);
		window.addEventListener('keyup', this.onKeyUp);
	}

	destroyed() {
		this.dirtyQueue.stop();
		this.draw.shutdown();
		window.removeEventListener('resize', this.onResize);
		window.removeEventListener('keydown', this.onKeyDown);
		window.removeEventListener('keyup', this.onKeyUp);
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
	padding: 0.5em;
	width: 20rem;
	display: flex;
	flex-direction: column;
	height: 100%;
	box-sizing: border-box;
}
.controlTop {
	display: flex;
	flex-direction: column;
	box-sizing: border-box;
	flex: 0 0 auto;
}
.controlBottom {
	display: flex;
	flex-direction: column;
	margin-top: 15px;
	box-sizing: border-box;
	flex: 0 1 auto;
}
.labelRow {
	//margin: 0.2em 0;
	display: flex;
	padding: 0.1em 0;
	border-radius: 0px;
	border: solid 1px rgba(0, 0, 0, 0);
	border-left: solid 4px rgba(0, 0, 0, 0);
	cursor: pointer;
}
.skLeft {
	width: 3em;
	text-align: center;
}
.skRight {
	margin-left: 1em;
}
.activeLabelRow {
	background-color: #f0f0d0;
	//border: solid 1px #888;
	font-weight: 800;
	border-left: solid 4px rgba(0, 0, 0, 255);
}
.titleBar {
	display: flex;
	align-content: center;
	margin-top: 0.8em;
	margin-bottom: 1.5em;
}
.backBtn {
	font-size: 1.5em;
	padding-left: 0.3em;
}
.title {
	flex: 1 0 auto;
	font-size: 1.5rem;
	margin-left: 1em;
	//text-align: center;
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
	pointer-events: none;
}
.scroller {
	margin: 30px;
	pointer-events: auto;
}
.overlayContainer {
	position: absolute;
	width: 100%;
	height: 100%;
	display: flex;
	flex-direction: column;
	align-items: center;
	align-content: center;
	pointer-events: none;
}
.overlayInfo {
	margin: 1rem;
	font-size: 1.2rem;
	background: rgba(255, 255, 255, 0.55);
	border-radius: 5px;
	padding: 0.5rem 0.7rem;
	display: flex;
	flex-direction: column;
	align-items: center;
	align-content: center;
	pointer-events: auto;
}
.optionsGroup {
	margin-top: 10px;
	padding: 5px;
	background-color: #f0f0f0;
	border: solid 1px #e0e0e0;
	border-radius: 3px;
	user-select: none;
}
.labelTxt {
	font-size: 15rem;
	color: #000;
	text-shadow: 0px 0px 20px #fff, 0px 0px 30px #fff, 0px 0px 40px #fff;
}
a {
	text-decoration: none;
	color: #000;
}
a:visited {
	color: #000;
}
.button {
	background-color: #eee;
}
.zoomAll {
	background: url("/maximize-2.svg");
}
</style>
