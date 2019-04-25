<template>
	<div class='reportTop'>
		<div v-for='dim of dimensions' :key='dim'>
			<h3>{{dim}}</h3>
			<div v-for='(details, val) in reportData.dimensions[dim]' :key='val' class='tab'>
				<div style='width: 2em; text-align: right'>{{details.count}}</div>
				<div style='width: 2em'></div>
				<div style='width: 8em'>{{val}}</div>
			</div>
		</div>
		<h2>Authors</h2>
		<div v-for='(details, author) in reportData.authors' :key='author' class='tab'>
			<div style='width: 2em; text-align: right'>{{details.count}}</div>
			<div style='width: 2em'></div>
			<div style='width: 8em'>{{author === '' ? '[anonymous]' : author}}</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Component, Vue } from 'vue-property-decorator';

@Component({})
export default class Report extends Vue {
	reportData: any = {
		dimensions: {
		},
		authors: {
		},
	};

	get dimensions(): string[] {
		return Object.keys(this.reportData.dimensions);
	}

	get authors(): string[] {
		return Object.keys(this.reportData.authors);
	}

	mounted() {
		fetch('/api/report').then((r) => {
			r.json().then((jr) => {
				this.reportData = jr;
			});
		});
	}
}
</script>

<style lang="scss" scoped>
.reportTop {
	padding-left: 1em;
}
.tab {
	display: flex;
}
</style>
