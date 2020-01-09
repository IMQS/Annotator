<template>
	<div
		class="outerLabelImage"
		@click="$emit('click')"
		:style="outerStyle"
		@mouseenter="isHot = true"
		@mouseleave="isHot = false"
	>
		<div class="iconDiv">
			<img class="icon" :src="iconURL" />
			<img class="bigIcon" :style="bigIconStyle" :src="iconURL" />
		</div>
		<div class="title">{{title}}</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';
import { DimensionValue } from '@/label';

@Component({})
export default class LabelImage extends Vue {
	@Prop({ default: null }) value!: DimensionValue;
	@Prop({ default: false }) isActive!: boolean;

	isHot: boolean = false;

	get iconURL(): string {
		return this.value.icon;
	}

	get title(): string {
		return this.value.title;
	}

	get bigIconStyle(): any {
		return {
			display: this.isHot ? 'inline' : 'none',
		};
	}

	get outerStyle(): any {
		if (!this.isActive)
			return {};
		return {
			'background-color': '#ddd',
		};
	}
}
</script>

<style lang="scss" scoped>
.outerLabelImage {
	cursor: pointer;
	display: flex;
	justify-content: flex-start;
	align-items: center;
}
.outerLabelImage:hover {
	background: #eee;
}
.iconDiv {
	position: relative;
	display: flex;
	min-width: 50px;
	align-items: center;
}
.icon {
	max-width: 24px;
	max-height: 36px;
}
.bigIcon {
	//opacity: 0.5;
	max-width: 80px;
	max-height: 80px;
	display: inline;
	position: absolute;
	z-index: 999;
	box-shadow: 0px 0px 10px rgba(0, 0, 0, 0.8);
	border: solid 1px #333;
	border-radius: 3px;
}
.title {
	font-size: 13px;
}
</style>
