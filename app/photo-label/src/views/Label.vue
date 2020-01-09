<template>
	<div class="outer">
		<div class="control">
			<div class="controlTop">
				<div class="titleBar">
					<router-link to="/" class="backBtn">⬅</router-link>
					<div class="title">{{dim ? dim.niceName : 'none'}}</div>
				</div>
				<div
					v-if="dim && isPolygonDim"
					style="display: flex; flex-direction: column; max-height: 400px; overflow-y: auto"
				>
					<label-image
						v-for="val in dim.values"
						:key="val.label"
						:value="val"
						:isActive="activeLabelCategory === val.label"
						@click="onLabelRowClick(val)"
					/>
				</div>
				<div v-else-if="dim">
					<div style="display: flex; margin-bottom: 0.5em">
						<div class="skLeft">⌨️</div>
						<div class="skRight">value</div>
					</div>
					<div
						v-for="val in dim.values"
						:key="val.label"
						class="labelRow"
						:class="{activeLabelRow: activeLabelCategory === val.label}"
						@click="onLabelRowClick(val)"
					>
						<div class="skLeft">{{dim.valueToShortcutKey[val.title]}}</div>
						<div class="skRight">{{val.title}}</div>
					</div>
					<div class="labelRow">
						<div class="skLeft">space</div>
						<div class="skRight" style="color: #a55">remove label</div>
					</div>
				</div>
				<div class="optionsGroup">
					<label>
						<input type="checkbox" v-model="drawTextOnLabels" />Show Labels
					</label>
				</div>
				<div v-if="isPolygonDim" style="margin: 1rem 0.5rem; font-size: 0.85rem; color: #777;">
					Press left/right keys to scan through the images. Hold down SHIFT to jump to images with labels on them.
					<br />
					<br />
					<span class="keyTxt">CTRL</span> delete vertex
					<br />
					<span class="keyTxt">ALT</span> delete polygon
					<br />
					<span class="keyTxt">Right Click</span> finish
					<br />
					<span class="keyTxt">CTRL + Right Click</span> finish circle
					<br />
					<span class="keyTxt">F</span> reset zoom
					<br />
					<span class="keyTxt">1..5</span> intensity 1..5
				</div>
				<div v-else style="margin: 1rem 0.5rem; font-size: 0.85rem; color: #777;">
					Press the shortcut key in the left column to label the image.
					<br />
					<br />Press left/right keys to scan through the images.
					<br />
					<br />Hold down
					<em>CTRL</em> and press left/right to apply the same label to the previous/next image.
				</div>
				<!--
				<div style='display: flex'><div class='skLeft'>←</div><div class='skRight'>previous</div></div>
				<div style='display: flex'><div class='skLeft'>→</div><div class='skRight'>next</div></div>
				-->
			</div>
			<div class="controlBottom">
				<div style="margin-bottom: 10px">Dataset</div>
				<dataset-picker
					@change="onDatasetChanged"
					style="max-height: 100%; overflow: auto; margin-left: 5px"
				/>
			</div>
		</div>
		<div class="canvasContainer">
			<canvas class="imgCanvas" ref="imgCanvas" :style="canvasStyle"></canvas>
			<div class="overlayContainer">
				<div class="overlayInfo">
					<div>{{currentImgName}}</div>
					<div style="display:flex; align-items: center">
						<div style="margin-right: 0.3em">🌞</div>
						<image-scroller style="width: 10rem" :pos="brightness" @change="onBrightnessChange"></image-scroller>
					</div>
				</div>
				<div class="labelTxt" :style="{'font-size': labelTxtFontSize}">{{wholeImageLabel}}</div>
				<div style="pointer-events: auto; position: absolute; right: 0px; top: 0px; padding: 10px">
					<svg-button icon="/maximize-2.svg" @click="draw.zoomAll()"></svg-button>
				</div>
			</div>
			<div class="scrollContainer">
				<image-scroller
					class="scroller"
					:style="{'pointer-events': showTimeSlider ? 'auto' : 'none'}"
					:pos="scrollPos"
					:ticks="scrollerTicks"
					@change="onScroll"
				></image-scroller>
			</div>
			<div
				v-if="showLabelChangeDropdown"
				class="dropdown"
				:style="{left: dropdownLeft + 'px', top: dropdownTop + 'px'}"
			>
				<div v-if="labelChangeDropdownIsIntensity">
					<div
						v-for="intensity of intensityValues"
						:key="intensity"
						class="dropdownRow"
						@click="onLabelChangeSelectIntensity(intensity)"
					>{{intensity}}</div>
				</div>
				<div v-else>
					<div
						v-for="lab of dim.values"
						:key="lab.label"
						class="dropdownRow"
						@click="onLabelChangeSelectCategory(lab)"
					>{{lab.title}}</div>
				</div>
			</div>
			<div class="intensity">{{activeLabelIntensity}}</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';
import { Dimension, DimensionSet, DimensionType, DirtyRegion, DirtyRegionQueue, LabelRegion, DimensionValue, LabelValue } from '@/label';
import ImageScroller from '@/components/ImageScroller.vue';
import DatasetPicker from '@/components/DatasetPicker.vue';
import SvgButton from '@/components/SvgButton.vue';
import LabelImage from '@/components/LabelImage.vue';
import * as draw from '@/draw';
import { ImageLabelSet } from '@/label';
import * as _ from 'underscore';

@Component({
	components: {
		ImageScroller,
		DatasetPicker,
		SvgButton,
		LabelImage,
	},
})
export default class Label extends Vue {
	@Prop(String) dimid!: string; // Dimension that we are labelling

	allPhotos: string[] = [];
	photoHasLabel: { [index: string]: boolean } = {}; // True if the given photo has at least one label in the current dimension
	scrollerTicks: number[] = []; // Tick marks for our time slider, showing where existing labels are
	selectedPhotos: string[] = []; // a subset of the photos in allPhotos, which match the prefix 'dataset'
	dataset: string = '';
	dimensions: DimensionSet = new DimensionSet();
	resizeDrawTimer: number = 0;
	imgIndex: number = -1; // index in selectedPhotos
	brightness: number = 0.5;
	ctrlKeyDown: boolean = false;
	waitingForImg: boolean = false;
	showTimeSlider: boolean = true;
	showLabelChangeDropdown: boolean = false;
	labelChangeDropdownIsIntensity: boolean = false;
	intensityValues: number[] = [1, 2, 3, 4, 5];
	dropdownRegion: LabelRegion | null = null;
	dropdownLeft: number = 0;
	dropdownTop: number = 0;
	dirtyQueue: DirtyRegionQueue = new DirtyRegionQueue();
	draw: draw.Draw = new draw.Draw();
	//isResizeBusy: boolean = false;
	fetchScrollerTicksDebounced = _.debounce(this.fetchScrollerTicks, 2000);

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

	get activeLabelCategory(): string {
		return this.draw.activeLabelCategory;
	}

	set activeLabelCategory(category: string) {
		this.draw.activeLabelCategory = category;
	}

	get activeLabelIntensity(): number {
		return this.draw.activeLabelIntensity;
	}

	set activeLabelIntensity(intensity: number) {
		this.draw.activeLabelIntensity = intensity;
	}

	get dim(): Dimension | null {
		return this.dimensions.fromID(this.dimid);
	}

	get isWholeImageDim(): boolean {
		if (this.dim === null)
			return true;
		return this.dim.type === DimensionType.WholeImage;
	}

	get isPolygonDim(): boolean {
		if (this.dim === null)
			return true;
		return this.dim.type === DimensionType.Polygon;
	}

	get isSemanticSegmentation(): boolean {
		if (this.dim === null)
			return true;
		return this.dim.type === DimensionType.Polygon && this.dim.isSemanticSegmentation;
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
		return lab.category;
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
		if (ev.key === 'f') {
			this.draw.zoomAll();
			return;
		} else if (ev.key === '1' || ev.key === '2' || ev.key === '3' || ev.key === '4' || ev.key === '5') {
			this.activeLabelIntensity = parseInt(ev.key, 10);
			return;
		}

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
			if (ev.shiftKey) {
				// When SHIFT is held down, seek to the prev/next image that has at least one label
				let i = this.imgIndex + seek;
				for (; i >= 0 && i < this.selectedPhotos.length; i += seek) {
					if (this.photoHasLabel[this.selectedPhotos[i]])
						break;
				}
				if (i >= 0 && i < this.selectedPhotos.length)
					this.imgIndex = i;
			} else {
				this.imgIndex += seek;
			}
			this.imgIndex = Math.max(0, Math.min(this.imgIndex, this.selectedPhotos.length - 1));
			this.saveScrollPosToLocalStorage();
			if (ev.ctrlKey && this.isWholeImageDim)
				this.setWholeImageLabel(this.activeLabelCategory);
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
					this.activeLabelCategory = val;
			}
		}
	}

	onKeyUp(ev: KeyboardEvent) {
		if (ev.key === 'Control')
			this.ctrlKeyDown = false;
	}

	onLabelRowClick(val: DimensionValue) {
		this.activeLabelCategory = val.label;
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
			this.computeScrollerTicks();
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
		this.computeScrollerTicks();
	}

	setWholeImageLabel(val: string) {
		this.activeLabelCategory = val;
		if (this.dim === null)
			return;
		this.$set(this.labels.wholeImageRegion.labels, this.dimid, val);
		this.dirtyQueue.push(new DirtyRegion(this.imgPath, this.labels.wholeImageRegion, this.dimid));
		this.fetchScrollerTicksDebounced();
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
			if (this.draw.curRegion && this.draw.curRegion.labels[this.dimid] === undefined) {
				// This path only gets hit when a new region is created
				this.$set(this.draw.curRegion.labels, this.dimid, new LabelValue(this.activeLabelCategory, this.activeLabelIntensity));
			}
			this.dirtyQueue.push(new DirtyRegion(this.imgPath, region, this.dimid));
		} else if (action === draw.Modification.Delete) {
			this.dirtyQueue.push(new DirtyRegion(this.imgPath, region, this.dimid));
		}

		this.fetchScrollerTicksDebounced();
	}

	onDrawEditStatechanged(newState: draw.EditState) {
		if (newState === draw.EditState.StartEdit) {
			// Hide the time slider, so that it doesn't get in the way
			this.showTimeSlider = false;
		} else if (newState === draw.EditState.EndEdit) {
			this.showTimeSlider = true;
		}
	}

	onChangeLabel(cursorX: number, cursorY: number, region: LabelRegion, dimension: string, isIntensity: boolean) {
		this.draw.freeze(); // freeze draw UI, so that it doesn't pick up mouse clicks on our dropdown
		this.dropdownRegion = region;
		this.showLabelChangeDropdown = true;
		this.labelChangeDropdownIsIntensity = isIntensity;
		this.dropdownLeft = cursorX;
		this.dropdownTop = cursorY;
	}

	onLabelChangeSelectIntensity(intensity: number) {
		this.dropdownRegion!.labels[this.dimid].intensity = intensity;
		this.postLabelDropdownChange();
	}

	onLabelChangeSelectCategory(lab: DimensionValue) {
		this.dropdownRegion!.labels[this.dimid].category = lab.label;
		this.postLabelDropdownChange();
	}

	postLabelDropdownChange() {
		this.showLabelChangeDropdown = false;
		this.onRegionModified(draw.Modification.Modify, this.dropdownRegion!);
		this.draw.paint();
		this.draw.unfreeze();
	}

	// Get all photos that have at least one label for the given dimension.
	// If performance is a problem here, we could also use ?prefix=<dataset>
	fetchScrollerTicks() {
		fetch('/api/db/get_folder_summary?dimension=' + encodeURIComponent(this.dimid)).then((r) => {
			r.json().then((jr) => {
				let photos = jr as string[];
				this.photoHasLabel = {};
				for (let p of photos)
					this.photoHasLabel[p] = true;
				this.computeScrollerTicks();
			});
		});
	}

	computeScrollerTicks() {
		let newTicks = [];
		for (let i = 0; i < this.selectedPhotos.length; i++) {
			if (this.photoHasLabel[this.selectedPhotos[i]]) {
				newTicks.push(i / (this.selectedPhotos.length - 1));
			}
		}
		this.scrollerTicks = newTicks;
	}

	mounted() {
		this.draw.onModifyRegion = this.onRegionModified;
		this.draw.onEditStateChange = this.onDrawEditStatechanged;
		this.draw.onChangeLabel = this.onChangeLabel;

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
		this.fetchScrollerTicks();
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
	margin: 30px 30px 4px 30px;
	pointer-events: auto;
}
.dropdown {
	position: absolute;
	background-color: #fff;
	min-width: 20px;
	min-height: 20px;
	box-shadow: 3px 3px 10px rgba(0, 0, 0, 0.5);
}
.dropdownRow {
	display: flex;
	//background-color: #fff;
	cursor: pointer;
	padding: 5px 10px;
}
.dropdownRow:hover {
	color: #fff;
	background-color: rgb(75, 102, 255);
}
.intensity {
	position: absolute;
	left: 0px;
	top: 0px;
	font-size: 100px;
	color: #000;
	text-shadow: 0px 0px 10px rgba(255, 255, 255, 1);
	background-color: rgba(255, 255, 255, 0.5);
	padding: 0px 20px;
	border-bottom-right-radius: 20px;
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
.iconLabelList {
	display: flex;
	flex-direction: column;
	max-height: 500px;
	overflow-y: auto;
	position: relative;
}
.labelTxt {
	font-size: 15rem;
	color: #000;
	text-shadow: 0px 0px 20px #fff, 0px 0px 30px #fff, 0px 0px 40px #fff;
}
.keyTxt {
	display: inline-block;
	font-style: italic;
	width: 10em;
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
