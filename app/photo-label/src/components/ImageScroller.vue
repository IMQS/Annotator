<template>
	<div
		ref="frame"
		class="scrollerFrame"
		@pointerdown="pointerdown"
		@pointermove="pointermove"
		@pointerup="pointerup"
	>
		<div ref="timeline" class="timeline">
			<canvas
				class="tickCanvas"
				ref="tickCanvas"
				:style="{visibility: ticks.length === 0 ? 'hidden' : 'visible'}"
			/>
			<div class="dotFrame" :style="{left: dotCssLeft}">
				<div class="dot">
					<div class="dotdot" />
				</div>
			</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';

@Component({})
export default class ImageScroller extends Vue {
	@Prop({ default: 0 }) pos!: number;
	@Prop({ default: () => [] }) ticks!: []; // Array of 'pos' values where there is interesting data (ie some existing labels)

	isDragging: boolean = false;
	dragOffset: number = 0;
	tickValues: number[] = [];

	get dotCssLeft(): string {
		return (this.pos * 100) + '%';
	}

	@Watch('ticks') onTicksChanged() {
		// Rebuild canvas
		let width = 2000;
		let count = [];
		for (let i = 0; i < width; i++)
			count.push(0);
		for (let p of this.ticks) {
			let slot = Math.floor(p * width);
			if (slot < 0)
				slot = 0;
			if (slot >= width)
				slot = width - 1;
			count[slot]++;
		}

		let addPixel = (rgba: Uint8ClampedArray, x: number, red: number, alpha: number) => {
			if (x < 0 || x >= rgba.length / 4)
				return;
			let p = x * 4;
			rgba[p + 0] = Math.min(255, rgba[p] + red);
			rgba[p + 1] = Math.min(255, rgba[p] + red);
			//rgba[p + 2] = Math.min(255, rgba[p] + red);
			rgba[p + 3] = Math.min(255, rgba[p + 3] + alpha);
		};

		let can = this.$refs.tickCanvas as HTMLCanvasElement;
		let height = 1;
		can.width = width;
		can.height = height;
		let cx = can.getContext('2d')!;
		let imgData = cx.getImageData(0, 0, width, height);
		for (let i = 0; i < width; i++) {
			if (count[i] !== 0) {
				let mag = count[i];
				// spread out the signal over 3 samples, to increase readability
				addPixel(imgData.data, i - 1, mag * 255, mag * 100);
				addPixel(imgData.data, i, mag * 255, mag * 200);
				addPixel(imgData.data, i + 1, mag * 255, mag * 100);
			}
		}
		cx.putImageData(imgData, 0, 0);
	}

	emitChange(newPos: number) {
		this.$emit('change', newPos);
	}

	pointermove(ev: PointerEvent) {
		if (this.isDragging) {
			this.emitChange(this.calcPos(ev));
		}
	}

	pointerup(ev: PointerEvent) {
		if (this.isDragging) {
			let frame = this.$refs.frame as HTMLElement;
			frame.releasePointerCapture(ev.pointerId);
			this.isDragging = false;
		}
	}

	pointerdown(ev: PointerEvent) {
		this.isDragging = true;
		let frame = this.$refs.frame as HTMLElement;
		frame.setPointerCapture(ev.pointerId);
		this.emitChange(this.calcPos(ev));
	}

	calcPos(ev: MouseEvent): number {
		let timeline = this.$refs.timeline as HTMLElement;
		let tr = timeline.getBoundingClientRect();
		let p = (ev.clientX - tr.left) / tr.width;
		p = Math.max(0, Math.min(p, 1));
		return p;
	}

	mounted() {
		this.onTicksChanged();
	}
}
</script>

<style lang="scss" scoped>
.scrollerFrame {
	width: 100%;
	// margin: 0 50px;
	box-sizing: border-box;
	height: 28px;
	//border-radius: 5px;
	//border: solid 1px #000;
	//background-color: #e1e;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
	position: relative;
	cursor: pointer;
	user-select: none;
}
.timeline {
	box-sizing: border-box;
	//margin: 0 30px;
	width: 90%;
	height: 7px;
	border-radius: 5px;
	border: solid 1px rgba(0, 0, 0, 0.6);
	background-color: rgba(200, 200, 200, 0.25);
	position: relative;
}
.tickCanvas {
	position: absolute;
	left: 0;
	top: 0;
	width: 100%;
	height: 5px;
	background-color: rgba(82, 175, 155, 0.5);
}
.dotFrame {
	width: 35px;
	height: 35px;
	margin-left: -18px;
	top: -15px;
	position: absolute;
	cursor: pointer;
	display: flex;
	justify-content: center;
	align-items: center;
	//background: #0dd;
}
.dot {
	width: 13px;
	height: 13px;
	border-radius: 100px;
	background-color: rgba(255, 200, 90, 0.6);
	border: solid 1px rgba(0, 0, 0, 0.3);
	display: flex;
	justify-content: center;
	align-items: center;
}
.dotdot {
	width: 3px;
	height: 3px;
	border-radius: 10px;
	background-color: rgba(0, 0, 0, 0.7);
}
</style>
