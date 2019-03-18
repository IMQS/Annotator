<template>
	<div ref='frame' class='scrollerFrame' @mousedown='mousedownFrame' @mousemove='mousemoveFrame' @mouseup='mouseupFrame'>
		<div ref='timeline' class='timeline'>
			<div class='dotFrame' :style='{left: dotCssLeft}' @mousedown='mousedownDot'>
				<div class='dot' />
			</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';

@Component({})
export default class ImageScroller extends Vue {

	dotWidth: number = 55;
	pos: number = 0;
	isDragging: boolean = false;
	dragOffset: number = 0;

	get dotCssLeft(): string {
		let timeline = this.$refs.timeline as HTMLElement;
		let timelineSize = 0;
		if (timeline !== undefined)
			timelineSize = timeline.clientWidth;
		return (this.pos * timelineSize - this.dotWidth / 2) + 'px';
	}

	mousedownFrame(ev: MouseEvent) {
		this.pos = this.calcPos(ev);
	}

	mousemoveFrame(ev: MouseEvent) {
		if (this.isDragging)
			this.pos = this.calcPos(ev);
	}

	mouseupFrame(ev: MouseEvent) {
		let frame = this.$refs.frame as HTMLElement;
		frame.releasePointerCapture(0);
	}

	mousedownDot(ev: MouseEvent) {
		this.isDragging = true;
		let frame = this.$refs.frame as HTMLElement;
		frame.setPointerCapture(0);
		this.pos = this.calcPos(ev);
	}

	calcPos(ev: MouseEvent): number {
		let timeline = this.$refs.timeline as HTMLElement;
		let tr = timeline.getBoundingClientRect();
		let p = (ev.clientX - tr.left) / tr.width;
		p = Math.max(0, Math.min(p, 1));
		return p;
	}
}
</script>

<style lang="scss" scoped>
.scrollerFrame {
	width: 100%;
	// margin: 0 50px;
	box-sizing: border-box;
	height: 50px;
	//border-radius: 5px;
	//border: solid 1px #000;
	//background-color: #e1e;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
	position: relative;
	cursor: pointer;
}
.timeline {
	box-sizing: border-box;
	//margin: 0 30px;
	width: 90%;
	height: 7px;
	border-radius: 5px;
	border: solid 1px rgba(0, 0, 0, 0.6);
	background-color: rgba(200, 200, 200, 0.2);
	position: relative;
}
.dotFrame {
	width: 55px; // must equal dotWidth
	height: 55px;
	top: -25px;
	position: absolute;
	cursor: pointer;
	display: flex;
	justify-content: center;
	align-items: center;
	//background: #0dd;
}
.dot {
	width: 17px;
	height: 17px;
	border-radius: 100px;
	background-color: rgba(255, 255, 255, 0.5);
	border: solid 1px rgba(255, 255, 255, 0.7);
}
</style>
